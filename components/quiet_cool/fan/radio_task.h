#pragma once
// Dedicated FreeRTOS task that owns ALL CC1101/SPI access after setup.
// ESPHome's loop() never touches the radio: RX events and TX requests cross
// task boundaries only through FreeRTOS queues. This keeps burst reception
// immune to main-loop stalls (WiFi/API/Bluetooth-proxy), which was the root
// cause of the old polling design missing remote button presses.
#include "cc1101.h"
#include "protocol.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>

namespace esphome {
namespace quiet_cool {

struct TxRequest {
    RemoteId id;
    uint8_t cmd;
};

struct RxEvent {
    RxCommand cmd;
    RemoteId sender;
    DecodeResult result;
    int8_t rssi_dbm;
    uint8_t lqi;
};

class RadioTask {
  public:
    RadioTask(CC1101 *radio, uint8_t gdo0_pin) : radio_(radio), gdo0_pin_(gdo0_pin) {}

    // Create queues, attach the GDO0 interrupt, and start the task.
    // The CC1101 must already be configured and in RX mode.
    bool start();

    // Queue a command for transmission (3 repeats, then back to RX).
    // Returns false if the TX queue is full.
    bool queue_tx(const RemoteId &id, uint8_t cmd);

    // Non-blocking; returns true if an event was retrieved.
    bool poll_rx(RxEvent &event);

    // Enable/disable the periodic WAKE state-query poll (uses `id` as sender).
    void set_wake_poll(const RemoteId &id, uint32_t interval_ms);

    // Diagnostics (read from any task)
    uint32_t rx_packet_count() const { return rx_packets_.load(); }
    uint32_t rx_dropped_count() const { return rx_dropped_.load(); }
    uint32_t overflow_count() const { return overflows_.load(); }
    uint32_t tx_fail_count() const { return tx_failures_.load(); }
    uint8_t last_marcstate() const { return last_marcstate_.load(); }

  private:
    static void task_entry_(void *arg);
    static void IRAM_ATTR gdo0_isr_(void *arg);
    void run_();
    void drain_rx_fifo_();
    void transmit_(const TxRequest &req);
    bool wait_gdo0_(bool level, uint32_t timeout_ms);
    void periodic_health_();

    CC1101 *radio_;
    uint8_t gdo0_pin_;

    TaskHandle_t task_{nullptr};
    SemaphoreHandle_t rx_signal_{nullptr};
    QueueHandle_t tx_queue_{nullptr};
    QueueHandle_t rx_queue_{nullptr};

    // WAKE poll config; written from the main task, read by the radio task.
    std::atomic<uint32_t> wake_interval_ms_{0};
    RemoteId wake_id_{};
    std::atomic<bool> wake_id_set_{false};

    std::atomic<uint32_t> rx_packets_{0};
    std::atomic<uint32_t> rx_dropped_{0};
    std::atomic<uint32_t> overflows_{0};
    std::atomic<uint32_t> tx_failures_{0};
    std::atomic<uint8_t> last_marcstate_{0};

    uint32_t last_wake_ms_{0};
    uint32_t last_health_ms_{0};
    uint32_t last_calibration_ms_{0};
};

}  // namespace quiet_cool
}  // namespace esphome
