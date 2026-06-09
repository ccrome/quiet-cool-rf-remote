#include "quiet_cool.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace quiet_cool {

static const char *TAG = "quiet_cool.fan";

static constexpr uint32_t PAIRING_WINDOW_MS = 60000;
static constexpr uint32_t WAKE_POLL_INTERVAL_MS = 30000;
static constexpr uint32_t CONFIRM_POLL_DELAY_MS = 2000;
static constexpr uint32_t RX_DEDUP_WINDOW_MS = 1000;
static constexpr uint32_t DIAG_LOG_INTERVAL_MS = 10000;

static std::string id_to_string(const RemoteId &id) {
    return format_hex_pretty(id.data(), id.size());
}

void QuietCoolFan::setup() {
    this->spi_setup();

    // Wake the chip per datasheet 19.1.2 (CS toggle, then wait for ready).
    this->cs_->digital_write(false);
    delayMicroseconds(10);
    this->cs_->digital_write(true);
    delayMicroseconds(45);

    this->radio_ = make_unique<CC1101>(static_cast<CC1101Spi *>(this));
    if (!this->radio_->reset_and_verify()) {
        ESP_LOGE(TAG, "CC1101 not detected — check SPI wiring");
        this->mark_failed();
        return;
    }

    this->radio_->configure(this->center_freq_mhz_, this->deviation_khz_);
    if (!this->radio_->recover_to_rx()) {
        ESP_LOGE(TAG, "CC1101 failed to enter RX mode");
        this->mark_failed();
        return;
    }

    // Restore paired remotes; seed with the YAML remote_id if given.
    this->paired_pref_ =
        global_preferences->make_preference<PairedRemotes>(this->get_object_id_hash());
    if (!this->paired_pref_.load(&this->paired_))
        this->paired_ = PairedRemotes{};
    if (this->yaml_id_set_)
        this->add_paired_(this->yaml_id_);

    this->radio_task_ = make_unique<RadioTask>(this->radio_.get(), this->gdo0_pin_);
    if (!this->radio_task_->start()) {
        this->mark_failed();
        return;
    }

    if (const RemoteId *id = this->primary_id_()) {
        ESP_LOGI(TAG, "Using remote ID %s (%u paired)", id_to_string(*id).c_str(),
                 this->paired_.count);
        this->radio_task_->set_wake_poll(*id, WAKE_POLL_INTERVAL_MS);
    } else {
        ESP_LOGW(TAG, "No remote paired — entering pairing mode. Press any button "
                      "on your QuietCool remote within 60 seconds.");
        this->start_pairing();
    }
}

fan::FanTraits QuietCoolFan::get_traits() {
    return fan::FanTraits(false, true, false, this->speed_count_);
}

void QuietCoolFan::loop() {
    if (this->radio_task_ == nullptr)
        return;

    RxEvent event;
    while (this->radio_task_->poll_rx(event)) {
        this->handle_rx_event_(event);
    }

    uint32_t now = millis();
    if (this->pairing_until_ms_ != 0 && now > this->pairing_until_ms_) {
        this->pairing_until_ms_ = 0;
        ESP_LOGW(TAG, "Pairing window closed (no remote heard)");
    }

    if (now - this->last_diag_log_ms_ >= DIAG_LOG_INTERVAL_MS) {
        this->last_diag_log_ms_ = now;
        ESP_LOGD(TAG, "Radio: MARCSTATE=0x%02X pkts=%u dropped=%u overflows=%u tx_fail=%u",
                 this->radio_task_->last_marcstate(), this->radio_task_->rx_packet_count(),
                 this->radio_task_->rx_dropped_count(), this->radio_task_->overflow_count(),
                 this->radio_task_->tx_fail_count());
    }
}

void QuietCoolFan::handle_rx_event_(const RxEvent &event) {
    const char *source = (event.rssi_dbm > -70) ? "FAN" : "REMOTE";
    ESP_LOGI(TAG, "RX [%s] from %s result=%d RSSI=%d LQI=%u", source,
             id_to_string(event.sender).c_str(), static_cast<int>(event.result),
             event.rssi_dbm, event.lqi);

    if (event.result != DecodeResult::OK && event.result != DecodeResult::OK_CORRECTED) {
        ESP_LOGW(TAG, "Undecodable packet from %s ignored (reason %d)",
                 id_to_string(event.sender).c_str(), static_cast<int>(event.result));
        return;
    }

    if (this->pairing_until_ms_ != 0 && millis() < this->pairing_until_ms_) {
        bool was_empty = this->paired_.count == 0;
        if (this->add_paired_(event.sender)) {
            this->pairing_until_ms_ = 0;
            ESP_LOGI(TAG, "Paired remote %s (%u total)",
                     id_to_string(event.sender).c_str(), this->paired_.count);
            if (was_empty)
                this->radio_task_->set_wake_poll(event.sender, WAKE_POLL_INTERVAL_MS);
        }
    }

    if (!this->is_paired_(event.sender)) {
        ESP_LOGD(TAG, "Command from unpaired remote %s ignored",
                 id_to_string(event.sender).c_str());
        return;
    }

    if (event.cmd.is_wake)
        return;  // WAKE carries no state

    // The remote repeats each command within a burst; collapse duplicates.
    uint32_t now = millis();
    bool duplicate = (now - this->last_rx_ms_ < RX_DEDUP_WINDOW_MS) &&
                     (event.cmd.is_off == this->last_rx_was_off_) &&
                     (event.cmd.speed == this->last_rx_cmd_speed_);
    this->last_rx_ms_ = now;
    this->last_rx_was_off_ = event.cmd.is_off;
    this->last_rx_cmd_speed_ = event.cmd.speed;
    if (!duplicate)
        this->apply_rx_command_(event.cmd);
}

void QuietCoolFan::apply_rx_command_(const RxCommand &cmd) {
    if (cmd.is_off) {
        this->state = false;
        this->speed = 0;
        ESP_LOGI(TAG, "RX sync: OFF");
    } else {
        this->state = true;
        this->speed = cmd_to_speed_level(cmd.speed, this->speed_count_);
        ESP_LOGI(TAG, "RX sync: ON speed=%d", this->speed);
    }
    this->publish_state();
}

void QuietCoolFan::control(const fan::FanCall &call) {
    if (call.get_state().has_value()) {
        bool new_state = *call.get_state();
        if (new_state) {
            if (call.get_speed().has_value()) {
                this->speed = *call.get_speed();
            } else if (this->speed == 0 || !this->state) {
                this->speed = 1;  // default to LOW when turning on cold
            }
            this->state = true;
        } else {
            this->state = false;
            this->speed = 0;
        }
    } else if (call.get_speed().has_value()) {
        this->speed = *call.get_speed();
        this->state = this->speed != 0;
    }

    uint8_t cmd;
    if (!this->state || this->speed == 0) {
        cmd = CMD_OFF;
    } else {
        auto hw_speed = static_cast<QuietCoolSpeed>(
            speed_level_to_cmd(this->speed, this->speed_count_));
        cmd = make_command(hw_speed, QUIETCOOL_DURATION_ON);
    }

    const RemoteId *id = this->primary_id_();
    if (id == nullptr) {
        ESP_LOGW(TAG, "No remote paired; cannot transmit (pair one first)");
    } else if (this->radio_task_ != nullptr && this->radio_task_->queue_tx(*id, cmd)) {
        ESP_LOGI(TAG, "Queued TX cmd=0x%02X (state=%s speed=%d)", cmd,
                 ONOFF(this->state), this->speed);
        // Publish optimistically now; a WAKE shortly after makes the fan report
        // its actual state, correcting HA if the command wasn't heard.
        this->set_timeout("confirm_wake", CONFIRM_POLL_DELAY_MS, [this]() {
            this->send_wake();
        });
    }

    this->publish_state();
}

void QuietCoolFan::send_wake() {
    const RemoteId *id = this->primary_id_();
    if (id != nullptr && this->radio_task_ != nullptr) {
        ESP_LOGD(TAG, "Queueing WAKE state query");
        this->radio_task_->queue_tx(*id, CMD_WAKE);
    }
}

void QuietCoolFan::start_pairing() {
    this->pairing_until_ms_ = millis() + PAIRING_WINDOW_MS;
    ESP_LOGI(TAG, "Pairing window open for %us — press any button on the remote",
             (unsigned) (PAIRING_WINDOW_MS / 1000));
}

void QuietCoolFan::clear_paired_remotes() {
    this->paired_ = PairedRemotes{};
    this->paired_pref_.save(&this->paired_);
    ESP_LOGI(TAG, "All paired remotes cleared");
}

bool QuietCoolFan::is_paired_(const RemoteId &id) const {
    for (uint8_t i = 0; i < this->paired_.count; i++) {
        if (memcmp(this->paired_.ids[i], id.data(), id.size()) == 0)
            return true;
    }
    return false;
}

bool QuietCoolFan::add_paired_(const RemoteId &id) {
    if (this->is_paired_(id))
        return false;
    if (this->paired_.count >= MAX_PAIRED_REMOTES) {
        // Evict the oldest non-primary entry (slot 1).
        for (uint8_t i = 1; i + 1 < MAX_PAIRED_REMOTES; i++)
            memcpy(this->paired_.ids[i], this->paired_.ids[i + 1], 7);
        this->paired_.count = MAX_PAIRED_REMOTES - 1;
    }
    memcpy(this->paired_.ids[this->paired_.count], id.data(), id.size());
    this->paired_.count++;
    this->paired_pref_.save(&this->paired_);
    return true;
}

const RemoteId *QuietCoolFan::primary_id_() const {
    if (this->paired_.count == 0)
        return nullptr;
    // PairedRemotes stores raw bytes; RemoteId is a std::array with identical layout.
    return reinterpret_cast<const RemoteId *>(&this->paired_.ids[0]);
}

void QuietCoolFan::dump_config() {
    LOG_FAN("", "QuietCool fan", this);
    ESP_LOGCONFIG(TAG, "  GDO0 Pin: %u", this->gdo0_pin_);
    ESP_LOGCONFIG(TAG, "  Frequency: %.3f MHz (deviation %.1f kHz)",
                  this->center_freq_mhz_, this->deviation_khz_);
    ESP_LOGCONFIG(TAG, "  Speed Count: %d", this->speed_count_);
    ESP_LOGCONFIG(TAG, "  Paired Remotes: %u", this->paired_.count);
    for (uint8_t i = 0; i < this->paired_.count; i++) {
        ESP_LOGCONFIG(TAG, "    [%u] %s", i,
                      format_hex_pretty(this->paired_.ids[i], 7).c_str());
    }
}

}  // namespace quiet_cool
}  // namespace esphome
