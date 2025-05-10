#include "empty_fan.h"
#include "esphome/core/log.h"
#include "quietcool.h"

namespace esphome {
    namespace empty_fan {
	
	static const char *TAG = "empty_fan.fan";

	void EmptyFan::setup() {
	    if (!this->pins_set_) {
		ESP_LOGE(TAG, "QuietCool pins not configured via YAML; radio not initialised");
		return;
	    }

	    if (this->qc_ == nullptr) {
		// Use standard VSPI pins (CLK18, MISO19, MOSI23) for ESP32 dev boards
		this->qc_.reset(new QuietCool(this->csn_pin_, this->gdo0_pin_, this->gdo2_pin_, 18, 19, 23));
	    }

	    this->qc_->begin();
	}

	fan::FanTraits EmptyFan::get_traits() {
	    return fan::FanTraits(this->oscillating_ != nullptr, false, this->direction_ != nullptr, 0);
	}

	void EmptyFan::control(const fan::FanCall &call) {
	    ESP_LOGD(TAG, "Control called: incoming state=%s, oscillating=%s, direction=%s", 
                call.get_state().has_value() ? (*call.get_state() ? "ON" : "OFF") : "<unchanged>",
                call.get_oscillating().has_value() ? (*call.get_oscillating() ? "ON" : "OFF") : "<unchanged>",
                call.get_direction().has_value() ? (*call.get_direction() == fan::FanDirection::FORWARD ? "FORWARD" : "REVERSE") : "<unchanged>");

	    bool old_state = this->state;
	    if (call.get_state().has_value())
		this->state = *call.get_state();
	    if (call.get_oscillating().has_value())
		this->oscillating = *call.get_oscillating();
	    if (call.get_direction().has_value())
		this->direction = *call.get_direction();

	    if (old_state != this->state) {
		ESP_LOGD(TAG, "Power state changed -> %s", this->state ? "ON" : "OFF");
		this->send_power_command_(this->state);
	    }

	    ESP_LOGV(TAG, "Post-update internal state: state=%s oscillating=%s direction=%s", 
              (this->state ? "ON" : "OFF"), (this->oscillating ? "ON" : "OFF"),
              this->direction == fan::FanDirection::FORWARD ? "FORWARD" : "REVERSE");

	    this->write_state_();
	    this->publish_state();
	}

	void EmptyFan::write_state_() {
	    ESP_LOGVV(TAG, "write_state_: driving pins: state=%s osc=%s dir=%s", 
               (this->state ? "ON" : "OFF"), (this->oscillating ? "ON" : "OFF"),
               this->direction == fan::FanDirection::FORWARD ? "FORWARD" : "REVERSE");

	    this->output_->set_state(this->state);
	    if (this->oscillating_ != nullptr)
		this->oscillating_->set_state(this->oscillating);
	    if (this->direction_ != nullptr)
		this->direction_->set_state(this->direction == fan::FanDirection::REVERSE);

	    ESP_LOGVV(TAG, "write_state_: output calls completed");
	}

	void EmptyFan::dump_config() { LOG_FAN("", "Empty fan", this); }

	void EmptyFan::send_power_command_(bool on) {
	    if (this->qc_ != nullptr) {
		ESP_LOGI(TAG, "Transmitting %s command via QuietCool", on ? "ON" : "OFF");
		this->qc_->send(on ? QUIETCOOL_SPEED_HIGH : QUIETCOOL_SPEED_OFF, QUIETCOOL_DURATION_ON);
	    } else {
		ESP_LOGW(TAG, "QuietCool not initialized yet; skipping transmit");
	    }
	}

    }  // namespace empty_fan
}  // namespace esphome

