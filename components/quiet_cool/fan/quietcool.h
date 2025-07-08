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

#define FREQ_MHZ  433.897

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
    uint8_t gdo2_pin;
    uint8_t sck_pin;
    uint8_t miso_pin;
    uint8_t mosi_pin;
    uint8_t remote_id[7];

    bool initCC1101();
    uint8_t readChipVersion();
    void processBitsFromBytes(const uint8_t* bytes, size_t byte_len, bool send_to_pin);
    void sendBitsFromBytes(const uint8_t* bytes, size_t byte_len);
    void sendRawData(const uint8_t* data, size_t len);
    void sendPacket(const uint8_t cmd_code);
    const uint8_t getCommand(QuietCoolSpeed speed, QuietCoolDuration duration);
    void logBits(const uint8_t* data, size_t len);
    // REMOTE_ID is now the name for the unique remote identifier

  public:
    QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi, const uint8_t* remote_id_in);
    void begin();
    void send(QuietCoolSpeed speed, QuietCoolDuration duration);
};

}  // namespace quiet_cool
}  // namespace esphome 
