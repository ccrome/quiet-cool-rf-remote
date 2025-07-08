#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "quietcool.h"
#include <memory>

namespace esphome {
    namespace quiet_cool {

	class QuietCoolFan : public Component, public fan::Fan, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1KHZ> {
	public:
	    void dump_config() override;
	    fan::FanTraits get_traits() override;
	    void setup() override;  // initialise radio
	    float get_setup_priority() const override { return setup_priority::BUS; }
	    void set_pins(uint8_t csn, uint8_t gdo0, uint8_t gdo2) {
	        this->csn_pin_ = csn;
	        this->gdo0_pin_ = gdo0;
	        this->gdo2_pin_ = gdo2;
	        this->pins_set_ = true;
	    }

	protected:
	    void control(const fan::FanCall &call) override;
	    void write_state_();
	    void send_power_command_(bool on);
	private:
	    std::unique_ptr<QuietCool> qc_;

	    uint8_t csn_pin_{};
	    uint8_t gdo0_pin_{};
	    uint8_t gdo2_pin_{};
	    float speed_{0.0f};
	    bool pins_set_{false};
	    std::array<uint8_t, 7> remote_id_{{0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2}};
	public:
	    void set_remote_id(const std::vector<uint8_t> &remote_id) {
	        for (size_t i = 0; i < 7 && i < remote_id.size(); ++i) remote_id_[i] = remote_id[i];
	    }
	};

    }  // namespace quiet_cool
}  // namespace esphome
