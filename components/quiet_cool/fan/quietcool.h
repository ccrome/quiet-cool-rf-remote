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

struct QuietCoolCommand {
    QuietCoolSpeed speed;
    QuietCoolDuration duration;
    uint8_t code;
    int8_t rssi_dbm;
    uint8_t lqi;
};

class QuietCool {
  private:
    static constexpr size_t REMOTE_ID_LEN = 7;
    static constexpr size_t RX_PAYLOAD_LEN = REMOTE_ID_LEN - 2 + 2;
    static constexpr size_t RX_STATUS_LEN = 2;
    static constexpr uint32_t RX_DEDUP_WINDOW_MS = 350;

    uint8_t csn_pin;
    uint8_t gdo0_pin;
    uint8_t gdo2_pin; // allow -1 for invalid
    uint8_t sck_pin;
    uint8_t miso_pin;
    uint8_t mosi_pin;
    uint8_t remote_id[7];
    float   center_freq_mhz;
    float   deviation_khz;
    bool initialized_{false};
    volatile bool listening_{false};
    volatile bool packet_ready_{false};
    uint8_t last_rx_code_{0};
    uint32_t last_rx_at_{0};

    static QuietCool *active_receiver_;
    static void handleGdo0Interrupt();

    bool initCC1101();
    uint8_t readChipVersion();
    void sendRawData(const uint8_t* data, size_t len);
    void sendPacket(const uint8_t cmd_code);
    const uint8_t getCommand(QuietCoolSpeed speed, QuietCoolDuration duration);
    void logBits(const uint8_t* data, size_t len);
    void configureTransmit();
    void configureReceive();
    void startListening();
    void restartReceiver();
    bool decodePayload(const uint8_t *payload, QuietCoolCommand &command) const;
    static int8_t decodeRssi(uint8_t raw_rssi);

  public:
    QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi, const uint8_t* remote_id_in, float freq_mhz, float deviation_khz);
    void begin();
    void send(QuietCoolSpeed speed, QuietCoolDuration duration);
    bool receive(QuietCoolCommand &command);
};

}  // namespace quiet_cool
}  // namespace esphome 
