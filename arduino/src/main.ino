#include <Arduino.h>
#include "quietcool.h"

/*
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

// --- Pin configuration ---
#define CSN_PIN   15
#define GDO0_PIN  13
#define GDO2_PIN  12
#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  23
// #define FREQ_MHZ  433.92
#define FREQ_MHZ  433.897

// Create QuietCool instance with pin configuration
QuietCool qc(CSN_PIN, GDO0_PIN, GDO2_PIN, SCK_PIN, MISO_PIN, MOSI_PIN);

// --- Arduino setup ---
void setup() {
    Serial.begin(115200);
    delay(1000);
    qc.begin();
}

void loop() {
    // Example: Send a command to turn on high speed for 1 hour
    qc.send(QUIETCOOL_SPEED_HIGH, QUIETCOOL_DURATION_1H);
    delay(20000);  // Wait 5 seconds

    // Example: Turn off the fan
    qc.send(QUIETCOOL_SPEED_OFF, QUIETCOOL_DURATION_1H);
    delay(20000);  // Wait 5 seconds
}

