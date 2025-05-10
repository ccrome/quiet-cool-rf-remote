#ifndef __QUIETCOOL_H__
#define __QUIETCOOL_H__

#include <Arduino.h>

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

typedef enum {
    QUIETCOOL_SPEED_HIGH,
    QUIETCOOL_SPEED_MEDIUM,
    QUIETCOOL_SPEED_LOW,
    QUIETCOOL_SPEED_OFF,
    QUIETCOOL_SPEED_LAST
} QuietCoolSpeed;

typedef enum {
    QUIETCOOL_DURATION_1H,
    QUIETCOOL_DURATION_2H,
    QUIETCOOL_DURATION_4H,
    QUIETCOOL_DURATION_8H,
    QUIETCOOL_DURATION_12H,
    QUIETCOOL_DURATION_ON,
    QUIETCOOL_DURATION_LAST
} QuietCoolDuration;

class QuietCool {
private:
    static const char* speed_settings[];
    static constexpr uint8_t TO_BIT(char c) { return (c == '1') ? 1 : 0; }

    uint8_t csn_pin;
    uint8_t gdo0_pin;
    uint8_t gdo2_pin;
    uint8_t sck_pin;
    uint8_t miso_pin;
    uint8_t mosi_pin;

    bool initCC1101();
    byte readChipVersion();
    void sendRawData(const char* data, byte len);
    void sendBits(const char* data, byte len);
    void sendPacket(const char* data, byte len);
    const char* getCommand(QuietCoolSpeed speed, QuietCoolDuration duration);

public:
    QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi);
    void begin();
    void send(QuietCoolSpeed speed, QuietCoolDuration duration);
};
#endif
