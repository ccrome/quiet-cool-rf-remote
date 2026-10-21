#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "quietcool.h"
#include <memory>

namespace esphome {
    namespace quiet_cool {

        class QuietCoolFan :
	    public Component,
	    public fan::Fan,
	    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1KHZ>
	{
        public:

            void dump_config() override;
            fan::FanTraits get_traits() override;
            void setup() override;  // initialise radio
            void loop() override;
            float get_setup_priority() const override { return setup_priority::BUS; }
            void set_pins(uint8_t csn, uint8_t gdo0, uint8_t gdo2) {
                this->csn_pin_ = csn;
                this->gdo0_pin_ = gdo0;
                this->gdo2_pin_ = gdo2;
                this->pins_set_ = true;
            }
	    void set_frequencies(float center_freq_mhz, float deviation_khz) {
		this->center_freq_mhz = center_freq_mhz;
		this->deviation_khz = deviation_khz;
	    }
            void set_speed_count(uint8_t speed_count) { this->speed_count_ = speed_count; }

        protected:
            void control(const fan::FanCall &call) override;
            void apply_received_command_(const QuietCoolCommand &command);
            QuietCoolSpeed to_radio_speed_(int speed) const;
            int from_radio_speed_(QuietCoolSpeed speed) const;
        private:
            std::unique_ptr<QuietCool> qc_;

            uint8_t csn_pin_{};
            uint8_t gdo0_pin_{};
            uint8_t gdo2_pin_{};
	    uint8_t speed_count_{3};
	    float center_freq_mhz{433.897};
	    float deviation_khz{10};
            bool pins_set_{false};
            bool remote_timer_active_{false};
            uint32_t remote_off_at_{0};
            std::array<uint8_t, 7> remote_id_{{0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2}};
        public:
            void set_remote_id(const std::vector<uint8_t> &remote_id) {
                for (size_t i = 0; i < 7 && i < remote_id.size(); ++i) remote_id_[i] = remote_id[i];
            }
        };

    }  // namespace quiet_cool
}  // namespace esphome
