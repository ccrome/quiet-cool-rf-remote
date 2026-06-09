#include "radio_task.h"
#include "esphome/core/log.h"

#include <Arduino.h>

namespace esphome {
namespace quiet_cool {

static const char *TAG = "quietcool.radio";

static constexpr UBaseType_t TASK_PRIORITY = 10;  // preempts the ESPHome loop task
static constexpr uint32_t TASK_STACK_BYTES = 6144;
static constexpr UBaseType_t TX_QUEUE_DEPTH = 4;
static constexpr UBaseType_t RX_QUEUE_DEPTH = 8;
static constexpr uint32_t HEALTH_INTERVAL_MS = 10000;
static constexpr uint32_t CALIBRATION_INTERVAL_MS = 300000;
static constexpr uint32_t TX_REPEATS = 3;
static constexpr uint32_t TX_REPEAT_GAP_MS = 18;

void IRAM_ATTR RadioTask::gdo0_isr_(void *arg) {
    auto *self = static_cast<RadioTask *>(arg);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->rx_signal_, &woken);
    if (woken)
        portYIELD_FROM_ISR();
}

void RadioTask::task_entry_(void *arg) { static_cast<RadioTask *>(arg)->run_(); }

bool RadioTask::start() {
    rx_signal_ = xSemaphoreCreateBinary();
    tx_queue_ = xQueueCreate(TX_QUEUE_DEPTH, sizeof(TxRequest));
    rx_queue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxEvent));
    if (rx_signal_ == nullptr || tx_queue_ == nullptr || rx_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate radio task queues");
        return false;
    }

    // GDO0 (IOCFG0=0x06) falls at end-of-packet — both RX and our own TX.
    pinMode(gdo0_pin_, INPUT);
    attachInterruptArg(digitalPinToInterrupt(gdo0_pin_), &RadioTask::gdo0_isr_,
                       this, FALLING);

    // Same core as the ESPHome loop (1), away from the WiFi/BT stack on core 0;
    // the higher priority lets it preempt the loop the moment a packet lands.
    BaseType_t ok = xTaskCreatePinnedToCore(&RadioTask::task_entry_, "quietcool_radio",
                                            TASK_STACK_BYTES, this, TASK_PRIORITY,
                                            &task_, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radio task");
        return false;
    }
    return true;
}

bool RadioTask::queue_tx(const RemoteId &id, uint8_t cmd) {
    TxRequest req{id, cmd};
    if (xQueueSend(tx_queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "TX queue full, dropping command 0x%02X", cmd);
        return false;
    }
    return true;
}

bool RadioTask::poll_rx(RxEvent &event) {
    return xQueueReceive(rx_queue_, &event, 0) == pdTRUE;
}

void RadioTask::set_wake_poll(const RemoteId &id, uint32_t interval_ms) {
    wake_id_ = id;
    wake_id_set_.store(true);
    wake_interval_ms_.store(interval_ms);
}

void RadioTask::run_() {
    ESP_LOGI(TAG, "Radio task started");
    uint32_t now = millis();
    last_wake_ms_ = now;
    last_health_ms_ = now;
    last_calibration_ms_ = now;

    while (true) {
        // Wake on packet-end interrupt, or every 20ms to service TX/health.
        if (xSemaphoreTake(rx_signal_, pdMS_TO_TICKS(20)) == pdTRUE) {
            drain_rx_fifo_();
        }

        TxRequest req;
        while (xQueueReceive(tx_queue_, &req, 0) == pdTRUE) {
            transmit_(req);
        }

        now = millis();

        uint32_t wake_interval = wake_interval_ms_.load();
        if (wake_interval > 0 && wake_id_set_.load() &&
            (now - last_wake_ms_) >= wake_interval) {
            last_wake_ms_ = now;
            transmit_(TxRequest{wake_id_, CMD_WAKE});
        }

        if (now - last_health_ms_ >= HEALTH_INTERVAL_MS) {
            last_health_ms_ = now;
            periodic_health_();
        }

        if (now - last_calibration_ms_ >= CALIBRATION_INTERVAL_MS) {
            last_calibration_ms_ = now;
            // Skip if a packet is mid-air; the next pass retries in 20ms.
            if (digitalRead(gdo0_pin_) == LOW && (radio_->rx_bytes() & 0x7F) == 0) {
                ESP_LOGD(TAG, "Periodic frequency calibration");
                radio_->calibrate();
            } else {
                last_calibration_ms_ = now - CALIBRATION_INTERVAL_MS + 1000;
            }
        }
    }
}

void RadioTask::drain_rx_fifo_() {
    while (true) {
        // Double-read RXBYTES per CC1101 errata; trust the lower value.
        uint8_t rb1 = radio_->rx_bytes();
        uint8_t rb2 = radio_->rx_bytes();
        uint8_t raw = (rb1 < rb2) ? rb1 : rb2;

        if (raw & 0x80) {
            ESP_LOGW(TAG, "RX FIFO overflow, recovering");
            overflows_++;
            radio_->recover_to_rx();
            return;
        }

        uint8_t available = raw & 0x7F;
        // TI errata SWRZ020: don't let the SPI read pointer catch the radio's
        // write pointer. Safe when 2+ frames are buffered, or when one full
        // frame is in and no reception is active (GDO0 low).
        bool can_read = (available >= 2 * FRAME_LEN) ||
                        (available >= FRAME_LEN && digitalRead(gdo0_pin_) == LOW);
        if (!can_read) {
            // Partial frame with no reception in progress = misalignment.
            if (available > 0 && available < FRAME_LEN && digitalRead(gdo0_pin_) == LOW) {
                ESP_LOGW(TAG, "RX FIFO misaligned (%u residual bytes), flushing", available);
                radio_->recover_to_rx();
            }
            return;
        }

        uint8_t frame[FRAME_LEN];
        radio_->read_burst(CC1101_FIFO, frame, FRAME_LEN);
        rx_packets_++;

        RxEvent event{};
        event.result = decode_any(frame, PACKET_LEN, event.sender, event.cmd);
        event.rssi_dbm = static_cast<int8_t>(frame[PACKET_LEN]) / 2 - 74;
        event.lqi = frame[PACKET_LEN + 1] & 0x7F;

        if (event.result == DecodeResult::ID_MISMATCH) {
            // Noise without the 0xAA fill — not worth crossing the queue.
            ESP_LOGV(TAG, "Noise packet discarded");
            continue;
        }
        if (xQueueSend(rx_queue_, &event, 0) != pdTRUE) {
            rx_dropped_++;
            ESP_LOGW(TAG, "RX event queue full, dropping packet");
        }
    }
}

bool RadioTask::wait_gdo0_(bool level, uint32_t timeout_ms) {
    uint32_t start = millis();
    while (digitalRead(gdo0_pin_) != (level ? HIGH : LOW)) {
        if (millis() - start > timeout_ms)
            return false;
        delay(1);
    }
    return true;
}

void RadioTask::transmit_(const TxRequest &req) {
    uint8_t packet[PACKET_LEN];
    build_packet(req.id, req.cmd, packet);
    ESP_LOGD(TAG, "TX cmd=0x%02X (x%u)", req.cmd, (unsigned) TX_REPEATS);

    for (uint32_t i = 0; i < TX_REPEATS; i++) {
        if (!radio_->go_idle()) {
            tx_failures_++;
            break;
        }
        radio_->flush_tx();
        radio_->write_burst(CC1101_FIFO, packet, PACKET_LEN);
        radio_->strobe(CC1101_STX);

        // ~9ms to sync word, ~73ms for the full packet at 2.4 kBaud. Bounded
        // waits: a wiring fault degrades to a logged error, not a WDT reset.
        if (!wait_gdo0_(true, 100) || !wait_gdo0_(false, 200)) {
            ESP_LOGE(TAG, "TX timeout (GDO0 stuck %s) — check GDO0 wiring",
                     digitalRead(gdo0_pin_) ? "high" : "low");
            tx_failures_++;
            break;
        }
        radio_->flush_tx();
        if (i + 1 < TX_REPEATS)
            delay(TX_REPEAT_GAP_MS);
    }

    if (!radio_->recover_to_rx()) {
        tx_failures_++;
        ESP_LOGE(TAG, "Failed to return to RX after TX");
    }
    // The TX itself raised and dropped GDO0; clear the stale RX signal.
    xSemaphoreTake(rx_signal_, 0);
}

void RadioTask::periodic_health_() {
    uint8_t state = radio_->marcstate();
    last_marcstate_.store(state);

    if (state == MARCSTATE_RXFIFO_OVERFLOW || state == MARCSTATE_TXFIFO_UNDERFLOW) {
        ESP_LOGW(TAG, "FIFO error state 0x%02X, recovering", state);
        overflows_++;
        radio_->recover_to_rx();
    } else if (state != MARCSTATE_RX) {
        ESP_LOGW(TAG, "Not in RX (MARCSTATE 0x%02X), forcing RX", state);
        radio_->recover_to_rx();
    }
}

}  // namespace quiet_cool
}  // namespace esphome
