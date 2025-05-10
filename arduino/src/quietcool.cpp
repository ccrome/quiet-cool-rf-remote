#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "quietcool.h"

// Command lookup table
const char* QuietCool::speed_settings[] = {
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100100110011001100110000", // PRE
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011000110110001000", // H1
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011001010110010000", // H2
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011010010110100000", // H4
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011100010111000000", // H8
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011110010111100000", // H12
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011111110111111000", // HON
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101011000010110000000", // HOFF
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010000110100001000", // M1
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010001010100010000", // M2
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010010010100100000", // M4
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010100010101000000", // M8
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010110010101100000", // M12
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010111110101111000", // MON
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101010000010100000000", // MOFF
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001000110010001000", // L1
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001001010010010000", // L2
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001010010010100000", // L4
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001100010011000000", // L8
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001110010011100000", // L12
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001111110011111000", // LON
    "0000101011010101010101010101010101010101010101010101010101010101010101010001011011101010000000110110010110000000011110111111100101001000010010000000", // LOFF
};

QuietCool::QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi) {
    csn_pin = csn;
    gdo0_pin = gdo0;
    gdo2_pin = gdo2;
    sck_pin = sck;
    miso_pin = miso;
    mosi_pin = mosi;
}

// --- Initialize CC1101 and verify communication ---
bool QuietCool::initCC1101() {
    ELECHOUSE_cc1101.setSpiPin(sck_pin, miso_pin, mosi_pin, csn_pin);
    byte version = readChipVersion();
    Serial.print("[INFO] CC1101 VERSION register: 0x");
    Serial.println(version, HEX);
    if (version == 0x14 || version == 0x04) {
        Serial.println("[INFO] CC1101 detected.");
    } else {
        Serial.println("[INFO] CC1101 NOT detected.");
        return false;
    }
    if (ELECHOUSE_cc1101.getCC1101()){        // Check the CC1101 Spi connection.
        Serial.println("Connection OK");
    }else{
        Serial.println("Connection Error");
        return false;
    }
    ELECHOUSE_cc1101.Init();

    // Basic configuration
    ELECHOUSE_cc1101.setMHZ(FREQ_MHZ);        // Set frequency first
    ELECHOUSE_cc1101.setPA(0);                // Start with low power

    // Configure for direct mode transmission
    ELECHOUSE_cc1101.setCCMode(1);            // Continuous mode
    ELECHOUSE_cc1101.setModulation(0);        // FSK modulation
    ELECHOUSE_cc1101.setDeviation(10);        // Frequency deviation
    ELECHOUSE_cc1101.setDRate(2.398);         // Data rate in kBaud

    // Disable all packet handling
    ELECHOUSE_cc1101.setSyncMode(0);          // No sync word
    ELECHOUSE_cc1101.setWhiteData(false);     // No whitening
    ELECHOUSE_cc1101.setManchester(false);    // No Manchester encoding
    ELECHOUSE_cc1101.setPktFormat(3);         // Raw data mode
    ELECHOUSE_cc1101.setCrc(0);               // No CRC
    ELECHOUSE_cc1101.setLengthConfig(0);      // Fixed length
    ELECHOUSE_cc1101.setPacketLength(0);      // No packet length
    ELECHOUSE_cc1101.setPRE(0);               // No preamble

    // Configure GDO pins for direct mode
    ELECHOUSE_cc1101.setGDO(gdo0_pin, gdo2_pin);  // Set both GDO pins
    delay(500);

    return true;
}

// --- Send raw data ---
#define TO_BIT(c) ((c=='1') ? 1 : 0)

void QuietCool::sendPacket(const char *data, byte len) {
    for (int i = 0; i < 3; i++) {
        sendRawData(data, len);
        delay(18);
    }
}

void QuietCool::sendBits(const char *data, byte len) {
    for (int bit = 0; bit < len; bit++) {
        digitalWrite(gdo0_pin, TO_BIT(data[bit]));
        delayMicroseconds(415);  // Adjust timing as needed
    }
}

void QuietCool::sendRawData(const char* data, byte len) {
    if (len == 0) {
        Serial.println("[ERROR] No data to send");
        return;
    }

    Serial.printf("Sending %d bits\n", len);

    const char *preamble = "00";
    digitalWrite(gdo0_pin, 0);
    noInterrupts();
    ELECHOUSE_cc1101.SetTx();
    sendBits(preamble, strlen(preamble));
    sendBits(data, len);
    ELECHOUSE_cc1101.setSidle();
    interrupts();

    delay(10);  // Give CC1101 time to enter IDLE mode
}

// --- Read VERSION register ---
byte QuietCool::readChipVersion() {
    return ELECHOUSE_cc1101.SpiReadReg(0xF1);
}

const char* QuietCool::getCommand(QuietCoolSpeed speed, QuietCoolDuration duration) {
    // Validate inputs
    if (speed >= QUIETCOOL_SPEED_LAST || duration >= QUIETCOOL_DURATION_LAST) {
        Serial.println("[ERROR] Invalid speed or duration");
        return speed_settings[7];  // Return HOFF as default
    }

    // Calculate base index for each speed
    const int BASE_HIGH = 1;    // H1 starts at index 1
    const int BASE_MEDIUM = 8;  // M1 starts at index 8
    const int BASE_LOW = 15;    // L1 starts at index 15
    const int INDEX_OFF = 7;    // HOFF is at index 7

    int index;
    switch (speed) {
        case QUIETCOOL_SPEED_HIGH:
            index = BASE_HIGH + duration;
            break;
        case QUIETCOOL_SPEED_MEDIUM:
            index = BASE_MEDIUM + duration;
            break;
        case QUIETCOOL_SPEED_LOW:
            index = BASE_LOW + duration;
            break;
        case QUIETCOOL_SPEED_OFF:
            index = INDEX_OFF;
            break;
        default:
            Serial.println("[ERROR] Unhandled speed case");
            return speed_settings[INDEX_OFF];  // Return HOFF as default
    }

    // Validate final index
    if (index < 0 || index >= sizeof(speed_settings)/sizeof(speed_settings[0])) {
        Serial.println("[ERROR] Calculated index out of bounds");
        return speed_settings[INDEX_OFF];  // Return HOFF as default
    }

    return speed_settings[index];
}

void QuietCool::begin() {
    // Initialize GDO pins as outputs for direct mode
    pinMode(gdo0_pin, OUTPUT);
    pinMode(gdo2_pin, OUTPUT);
    digitalWrite(gdo0_pin, LOW);
    digitalWrite(gdo2_pin, LOW);

    Serial.println("[BOOT] Starting CC1101 TX setup...");
    if (!initCC1101()) {
        Serial.println("[ERROR] CC1101 not detected. Halting.");
        while (true);  // Halt
    }
    Serial.println("[BOOT] CC1101 TX setup complete");
}

void QuietCool::send(QuietCoolSpeed speed, QuietCoolDuration duration) {
    const char* cmd = getCommand(speed, duration);
    if (cmd) {
        sendPacket(cmd, strlen(cmd)-1);
    }
}
