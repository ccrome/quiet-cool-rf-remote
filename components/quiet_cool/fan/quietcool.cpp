#include "quietcool.h"
#include "esphome/core/log.h"
#include "ELECHOUSE_CC1101_SRC_DRV.h"

namespace esphome {
namespace quiet_cool {

static const char *TAG = "quietcool";

#define SYNC "_00010101_10101010_10101010_10101010_10101010_10101010_10101010_10101010_10101010"

// original ->         000101101110101000000011011001011000000001111011111110010 
#define PREAMBLE SYNC "00101101_11010100_00000110_11001011_00000000_11110111_11110010"

// Command lookup table
const char* QuietCool::speed_settings[] = {
    PREAMBLE "01100110_01100110", // PRE
    PREAMBLE "10110001_10110001", // H1
    PREAMBLE "10110010_10110010", // H2
    PREAMBLE "10110100_10110100", // H4
    PREAMBLE "10111000_10111000", // H8
    PREAMBLE "10111100_10111100", // H12
    PREAMBLE "10111111_10111111", // HON
    PREAMBLE "10110000_10110000", // HOFF
    PREAMBLE "10100001_10100001", // M1
    PREAMBLE "10100010_10100010", // M2
    PREAMBLE "10100100_10100100", // M4
    PREAMBLE "10101000_10101000", // M8
    PREAMBLE "10101100_10101100", // M12
    PREAMBLE "10101111_10101111", // MON
    PREAMBLE "10100000_10100000", // MOFF
    PREAMBLE "10010001_10010001", // L1
    PREAMBLE "10010010_10010010", // L2
    PREAMBLE "10010100_10010100", // L4
    PREAMBLE "10011000_10011000", // L8
    PREAMBLE "10011100_10011100", // L12
    PREAMBLE "10011111_10011111", // LON
    PREAMBLE "10010000_10010000", // LOFF
};

QuietCool::QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi)
    : csn_pin(csn), gdo0_pin(gdo0), gdo2_pin(gdo2), sck_pin(sck), miso_pin(miso), mosi_pin(mosi) {}

// --- Initialize CC1101 and verify communication ---
bool QuietCool::initCC1101() {
    ESP_LOGD(TAG, "sck:%d, miso:%d, mosi:%d, csn:%d\n", sck_pin, miso_pin, mosi_pin, csn_pin);
    ELECHOUSE_cc1101.setSpiPin(sck_pin, miso_pin, mosi_pin, csn_pin);
    int tries = 10;
    bool detected = false;
    while (tries--) {
	uint8_t version = readChipVersion();
	ESP_LOGI(TAG, "CC1101 VERSION READ: 0x%02X", version);
	if (version == 0x14 || version == 0x04) {
	    ESP_LOGI(TAG, "CC1101 detected!");
	    detected = true;
	    break;
	}
    }
    if (!detected) {
	ESP_LOGE(TAG, "CC1101 not detected!");
	return false;
    }
    if (!ELECHOUSE_cc1101.getCC1101()) {
        ESP_LOGE(TAG, "CC1101 connection error");
        return false;
    }
    ELECHOUSE_cc1101.Init();

    // Basic configuration
    ELECHOUSE_cc1101.setMHZ(FREQ_MHZ);
    ELECHOUSE_cc1101.setPA(0);

    // Configure for direct mode transmission
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.setModulation(0);
    ELECHOUSE_cc1101.setDeviation(10);
    ELECHOUSE_cc1101.setDRate(2.398);

    // Disable all packet handling
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setWhiteData(false);
    ELECHOUSE_cc1101.setManchester(false);
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setLengthConfig(0);
    ELECHOUSE_cc1101.setPacketLength(0);
    ELECHOUSE_cc1101.setPRE(0);

    // Configure GDO pins for direct mode
    ELECHOUSE_cc1101.setGDO(gdo0_pin, gdo2_pin);
    delay(500);

    return true;
}

void QuietCool::sendPacket(const char *data, uint8_t len) {
    for (int i = 0; i < 3; i++) {
        sendRawData(data, len);
        delay(18);
    }
}

void QuietCool::sendBits(const char *data, uint8_t len) {
    for (int bit = 0; bit < len; bit++) {
	uint8_t b;
	switch (data[bit]) {
	case '1': b = 1; break;
	case '0': b = 0; break;
	default: continue;
	}
        digitalWrite(gdo0_pin, b);
        delayMicroseconds(415);
    }
}

void QuietCool::sendRawData(const char* data, uint8_t len) {
    if (len == 0) {
        ESP_LOGE(TAG, "No data to send");
        return;
    }

    ESP_LOGD(TAG, "Sending %d bits", len);

    digitalWrite(gdo0_pin, 0);
    noInterrupts();
    ELECHOUSE_cc1101.SetTx();
    sendBits(data, len);
    ELECHOUSE_cc1101.setSidle();
    interrupts();

    delay(10);
}

uint8_t QuietCool::readChipVersion() {
    return ELECHOUSE_cc1101.SpiReadReg(0xF1);
}

const char* QuietCool::getCommand(QuietCoolSpeed speed, QuietCoolDuration duration) {
    if (speed >= QUIETCOOL_SPEED_LAST || duration >= QUIETCOOL_DURATION_LAST) {
        ESP_LOGE(TAG, "Invalid speed or duration");
        return speed_settings[7];
    }

    const int BASE_HIGH = 1;
    const int BASE_MEDIUM = 8;
    const int BASE_LOW = 15;
    const int INDEX_OFF = 7;

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
            ESP_LOGE(TAG, "Unhandled speed");
            return speed_settings[INDEX_OFF];
    }

    const int total = sizeof(speed_settings) / sizeof(speed_settings[0]);
    if (index < 0 || index >= total) {
        ESP_LOGE(TAG, "Index out of bounds");
        return speed_settings[INDEX_OFF];
    }
    return speed_settings[index];
}

void QuietCool::begin() {
    ESP_LOGD(TAG, "gdo0_pin = %d, gdo2_pin = %d\n", gdo0_pin, gdo2_pin);
    pinMode(gdo0_pin, OUTPUT);
    pinMode(gdo2_pin, OUTPUT);
    digitalWrite(gdo0_pin, LOW);
    digitalWrite(gdo2_pin, LOW);

    ESP_LOGI(TAG, "Starting CC1101 setup");
    if (!initCC1101()) {
        ESP_LOGE(TAG, "CC1101 not detected");
        return;
    }
    ESP_LOGI(TAG, "CC1101 ready");
}

void QuietCool::send(QuietCoolSpeed speed, QuietCoolDuration duration) {
    ESP_LOGI(TAG, "send(%d, %d)", speed, duration);
    const char* cmd = getCommand(speed, duration);
    ESP_LOGI(TAG, "%s", cmd);
    if (cmd)
        sendPacket(cmd, strlen(cmd)-1);
}

}  // namespace quiet_cool
}  // namespace esphome 
