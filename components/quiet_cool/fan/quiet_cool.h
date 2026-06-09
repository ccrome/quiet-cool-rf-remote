#pragma once

#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/spi/spi.h"
#include "cc1101.h"
#include "protocol.h"
#include "radio_task.h"
#include <memory>
#include <vector>

namespace esphome {
namespace quiet_cool {

// Paired remote IDs persisted to flash. TX uses the first entry; RX state
// sync accepts any of them.
static constexpr uint8_t MAX_PAIRED_REMOTES = 4;
struct PairedRemotes {
    uint8_t count{0};
    uint8_t ids[MAX_PAIRED_REMOTES][7]{};
} __attribute__((packed));

class QuietCoolFan : public Component,
                     public fan::Fan,
                     public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                           spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ>,
                     public CC1101Spi {
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    fan::FanTraits get_traits() override;
    float get_setup_priority() const override { return setup_priority::DATA; }

    void set_pins(uint8_t gdo0, uint8_t gdo2) {
        this->gdo0_pin_ = gdo0;
        this->gdo2_pin_ = gdo2;
    }
    void set_frequencies(float center_freq_mhz, float deviation_khz) {
        this->center_freq_mhz_ = center_freq_mhz;
        this->deviation_khz_ = deviation_khz;
    }
    void set_speed_count(int speed_count) { this->speed_count_ = speed_count; }
    void set_remote_id(const std::vector<uint8_t> &remote_id) {
        for (size_t i = 0; i < 7 && i < remote_id.size(); ++i)
            this->yaml_id_[i] = remote_id[i];
        this->yaml_id_set_ = true;
    }

    // Query the fan's current state (it answers WAKE with its status).
    void send_wake();
    // Open a pairing window: the next valid packet from any remote stores
    // that remote's ID in flash.
    void start_pairing();
    void clear_paired_remotes();

    // CC1101Spi: hand the driver ESPHome's SPI bus, one transaction per access.
    void begin() override { this->enable(); }
    uint8_t transfer(uint8_t value) override { return this->transfer_byte(value); }
    void end() override { this->disable(); }

  protected:
    void control(const fan::FanCall &call) override;

    bool is_paired_(const RemoteId &id) const;
    bool add_paired_(const RemoteId &id);
    const RemoteId *primary_id_() const;
    void handle_rx_event_(const RxEvent &event);
    void apply_rx_command_(const RxCommand &cmd);

    std::unique_ptr<CC1101> radio_;
    std::unique_ptr<RadioTask> radio_task_;

    uint8_t gdo0_pin_{0};
    uint8_t gdo2_pin_{0};  // wired but unused; reserved
    float center_freq_mhz_{433.897f};
    float deviation_khz_{10.0f};
    int speed_count_{3};

    RemoteId yaml_id_{};
    bool yaml_id_set_{false};
    PairedRemotes paired_{};
    ESPPreferenceObject paired_pref_;

    uint32_t pairing_until_ms_{0};

    // RX dedup (the remote sends each command in a multi-packet burst)
    uint8_t last_rx_cmd_speed_{0};
    bool last_rx_was_off_{false};
    uint32_t last_rx_ms_{0};

    uint32_t last_diag_log_ms_{0};
};

}  // namespace quiet_cool
}  // namespace esphome
