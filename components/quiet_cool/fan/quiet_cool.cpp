#include "quiet_cool.h"
#include "esphome/core/log.h"
#include "quietcool.h"
#include <algorithm>

namespace esphome {
    namespace quiet_cool {
        
        static const char *TAG = "quiet_cool.fan";

        void QuietCoolFan::setup() {
            if (!this->pins_set_) {
                ESP_LOGE(TAG, "QuietCool pins not configured via YAML; radio not initialised");
                return;
            }

            if (this->qc_ == nullptr) {
                // Use standard VSPI pins (CLK18, MISO19, MOSI23) for ESP32 dev boards
                this->qc_.reset(new QuietCool(this->csn_pin_, this->gdo0_pin_, this->gdo2_pin_, 18, 19, 23, remote_id_.data(), center_freq_mhz, deviation_khz));
            }

            this->qc_->begin();
            ESP_LOGD(TAG, "QuietCool initialized");
        }

        fan::FanTraits QuietCoolFan::get_traits() {
            return fan::FanTraits(false, true, false, this->speed_count_);
        }

        QuietCoolSpeed QuietCoolFan::to_radio_speed_(int speed) const {
            if (speed <= 1)
                return QUIETCOOL_SPEED_LOW;
            if (this->speed_count_ == 2 || speed >= 3)
                return QUIETCOOL_SPEED_HIGH;
            return QUIETCOOL_SPEED_MEDIUM;
        }

        int QuietCoolFan::from_radio_speed_(QuietCoolSpeed speed) const {
            switch (speed) {
                case QUIETCOOL_SPEED_LOW:
                    return 1;
                case QUIETCOOL_SPEED_MEDIUM:
                    return this->speed_count_ == 3 ? 2 : 0;
                case QUIETCOOL_SPEED_HIGH:
                    return this->speed_count_;
                default:
                    return 0;
            }
        }

        void QuietCoolFan::loop() {
            if (this->qc_ != nullptr) {
                QuietCoolCommand command{};
                if (this->qc_->receive(command))
                    this->apply_received_command_(command);
            }

            if (this->remote_timer_active_ &&
                static_cast<int32_t>(millis() - this->remote_off_at_) >= 0) {
                this->remote_timer_active_ = false;
                this->state = false;
                ESP_LOGI(TAG, "Remote timer elapsed; publishing fan OFF");
                this->publish_state();
            }
        }

        void QuietCoolFan::control(const fan::FanCall &call) {
            const int requested_speed = call.get_speed().value_or(this->speed > 0 ? this->speed : 1);
            const bool requested_state = call.get_state().value_or(this->state);
            ESP_LOGD(TAG, "Control called: state=%s, speed=%d%s",
                     call.get_state().has_value() ? (*call.get_state() ? "ON" : "OFF") : "<unchanged>",
                     requested_speed, call.get_speed().has_value() ? "" : " (unchanged)");

            QuietCoolSpeed qcspd = QUIETCOOL_SPEED_LOW;
            QuietCoolDuration qcdur;
            if (!requested_state) {
                // The captured physical remote uses B0 for OFF.
                qcspd = QUIETCOOL_SPEED_HIGH;
                qcdur = QUIETCOOL_DURATION_OFF;
            } else {
                qcdur = QUIETCOOL_DURATION_ON;
                qcspd = this->to_radio_speed_(requested_speed);
            }

            if (this->qc_ != nullptr)
                this->qc_->send(qcspd, qcdur);

            this->state = requested_state;
            if (requested_state)
                this->speed = std::max(1, std::min(static_cast<int>(this->speed_count_), requested_speed));
            this->remote_timer_active_ = false;

            ESP_LOGV(TAG, "Post-update internal state: state=%s speed=%d",
                     this->state ? "ON" : "OFF", this->speed);
            this->publish_state();
        }

        void QuietCoolFan::apply_received_command_(const QuietCoolCommand &command) {
            if (command.duration == QUIETCOOL_DURATION_OFF) {
                this->state = false;
                this->remote_timer_active_ = false;
            } else {
                const int received_speed = this->from_radio_speed_(command.speed);
                if (received_speed == 0) {
                    ESP_LOGW(TAG, "Ignoring MEDIUM command configured for a 2-speed fan");
                    return;
                }
                this->state = true;
                this->speed = received_speed;

                if (command.duration == QUIETCOOL_DURATION_ON) {
                    this->remote_timer_active_ = false;
                } else {
                    const uint32_t duration_ms =
                        static_cast<uint32_t>(command.duration) * 60UL * 60UL * 1000UL;
                    this->remote_off_at_ = millis() + duration_ms;
                    this->remote_timer_active_ = true;
                }
            }

            ESP_LOGI(TAG, "Publishing physical remote state: %s, speed %d, command 0x%02X",
                     this->state ? "ON" : "OFF", this->speed, command.code);
            this->publish_state();
        }

        void QuietCoolFan::dump_config() {
            LOG_FAN("", "QuietCool fan", this);
            ESP_LOGCONFIG(TAG, "  Speed count: %u", this->speed_count_);
        }
    }  // namespace quiet_cool
}  // namespace esphome
