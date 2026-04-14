#pragma once
#include <stdint.h>
#include <stddef.h>

/*
  CC1101 Pin Connections:
  hdr   cc1101
  1      19  GND
  2      18  VCC
  3      6   GDO0
  4      7   CSn
  5      1   SCK
  6      20  MOSI
  7      2   MISO
  8      3   GDO2
*/

namespace esphome {
namespace quiet_cool {

enum QuietCoolSpeed {
    QUIETCOOL_SPEED_HIGH   =  0xB0,
    QUIETCOOL_SPEED_MEDIUM =  0xA0,
    QUIETCOOL_SPEED_LOW    =  0x90,
    QUIETCOOL_SPEED_LAST
};

enum QuietCoolDuration {
    QUIETCOOL_DURATION_1H   = 0x01,
    QUIETCOOL_DURATION_2H   = 0x02,
    QUIETCOOL_DURATION_4H   = 0x04,
    QUIETCOOL_DURATION_8H   = 0x08,
    QUIETCOOL_DURATION_12H  = 0x0C,
    QUIETCOOL_DURATION_ON   = 0x0F,
    QUIETCOOL_DURATION_OFF  = 0x00,
    QUIETCOOL_DURATION_LAST
};

class QuietCool {
  private:
    static constexpr uint8_t TO_BIT(char c) { return (c == '1') ? 1 : 0; }

    uint8_t csn_pin;
    uint8_t gdo0_pin;
    uint8_t gdo2_pin; // allow -1 for invalid
    uint8_t sck_pin;
    uint8_t miso_pin;
    uint8_t mosi_pin;
    uint8_t remote_id[7];
    float   center_freq_mhz;
    float   deviation_khz;
    uint8_t  preamble_byte{0xAA};
    bool     fsctrl0_override_enabled{false};
    uint8_t  fsctrl0_override_value{0};
    bool     verify_ack{false};
    uint32_t ack_timeout_ms{800};
    uint8_t  max_send_attempts{5};

    bool initCC1101();
    bool awaitAck(uint32_t timeout_ms);
    uint8_t readChipVersion();
    void processBitsFromBytes(const uint8_t* bytes, size_t byte_len, bool send_to_pin);
    void sendRawData(const uint8_t* data, size_t len);
    void sendPacket(const uint8_t cmd_code);
    const uint8_t getCommand(QuietCoolSpeed speed, QuietCoolDuration duration);
    void logBits(const uint8_t* data, size_t len);
    // REMOTE_ID is now the name for the unique remote identifier

  public:
    QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi, const uint8_t* remote_id_in, float freq_mhz, float deviation_khz);
    void begin();
    void send(QuietCoolSpeed speed, QuietCoolDuration duration);

    // The preamble byte following the leading 0x15. Default 0xAA matches the
    // original reverse-engineered protocol; some hubs (e.g. Stealth Pro 7.0 with
    // the latest firmware) reject this and require 0x55 instead. 0x15 0xAA has
    // a `1,1` bit at the byte boundary that breaks strict bit-sync decoders;
    // 0x55 gives a purely alternating bit stream.
    void setPreambleByte(uint8_t b) { preamble_byte = b; }

    // Override FSCTRL0 (frequency-synthesizer offset, register 0x0C) after
    // setMHZ(). ELECHOUSE's Calibrate() writes a frequency-mapped value here
    // (at 433 MHz that's ~35, ~+55 kHz offset). The register is 8-bit two's
    // complement; each unit is f_XOSC/2^14 ≈ 1.587 kHz. Pass 0 to cancel the
    // library's offset; pass other values to dial in per-unit crystal drift.
    void setFsctrl0Override(uint8_t v) { fsctrl0_override_enabled = true; fsctrl0_override_value = v; }

    // Bidirectional protocol: when enabled, after each transmit the radio
    // switches to RX and listens for the hub's ACK packet (verified to exist
    // via SDR analysis — same 2-FSK / 433.92 MHz / preamble+ID structure as
    // the remote's TX, arriving ~250 ms later in groups of 3). If no ACK is
    // detected within `timeout_ms` the packet is retransmitted, up to `max`
    // attempts total. Matches the real remote's out-of-range error behavior.
    void setVerifyAck(bool v) { verify_ack = v; }
    void setAckTimeoutMs(uint32_t v) { ack_timeout_ms = v; }
    void setMaxSendAttempts(uint8_t v) { max_send_attempts = v; }
};

}  // namespace quiet_cool
}  // namespace esphome 
