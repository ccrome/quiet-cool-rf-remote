#include "quiet_cool.h"
#include "esphome/core/log.h"
#include "quietcool.h"

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
		this->qc_.reset(new QuietCool(this->csn_pin_, this->gdo0_pin_, this->gdo2_pin_, 18, 19, 23, remote_id_.data()));
	    }

	    this->qc_->begin();
	    ESP_LOGD(TAG, "QuietCool initialized");
	}

	fan::FanTraits QuietCoolFan::get_traits() {
	    return fan::FanTraits(false, true, false, 3);
	}

	void QuietCoolFan::control(const fan::FanCall &call) {
	    float inc_speed = call.get_speed().value_or(-1.0f);
	    ESP_LOGD(TAG, "Control called: state=%s, speed=%s", 
		     call.get_state().has_value() ? (*call.get_state() ? "ON" : "OFF") : "<unchanged>",
		     call.get_speed().has_value() ? (std::to_string(inc_speed)).c_str() : "<unchanged>");
	    bool old_state = this->state;
	    if (call.get_state().has_value())
		this->state = *call.get_state();

	    QuietCoolSpeed qcspd = QUIETCOOL_SPEED_LOW;
	    QuietCoolDuration qcdur = QUIETCOOL_DURATION_ON;
	    if (call.get_speed().has_value()) {
		this->speed_ = *call.get_speed();
		if (this->speed_ < 0.5) qcdur = QUIETCOOL_DURATION_OFF;
		else if (this->speed_ < 1.5) qcspd = QUIETCOOL_SPEED_LOW;
		else if (this->speed_ < 2.5) qcspd = QUIETCOOL_SPEED_MEDIUM;
		else if (this->speed_ < 3.5) qcspd = QUIETCOOL_SPEED_HIGH;
	    }
	    if (this->qc_) this->qc_->send(qcspd, qcdur);


	    ESP_LOGV(TAG, "Post-update internal state: state=%s speed=%s", 
		     (this->state ? "ON" : "OFF"),
		     (std::to_string(this->speed_)).c_str());

	    this->write_state_();
	    this->publish_state();
	}

	void QuietCoolFan::write_state_() {
	    ESP_LOGVV(TAG, "write_state_: driving pins: state=%s ", 
		      (this->state ? "ON" : "OFF"));
	    ESP_LOGVV(TAG, "write_state_: output calls completed");
	}

	void QuietCoolFan::dump_config() { LOG_FAN("", "QuietCool fan", this); }
    }  // namespace quiet_cool
}  // namespace esphome

