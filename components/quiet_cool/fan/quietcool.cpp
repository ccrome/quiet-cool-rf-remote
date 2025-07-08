#include "quietcool.h"
#include "esphome/core/log.h"
#include "ELECHOUSE_CC1101_SRC_DRV.h"

namespace esphome {
namespace quiet_cool {

static const char *TAG = "quietcool";

const uint8_t SYNC[] = {0x15, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
const uint8_t REMOTE_ID[] = {0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2};
#define SYNC_LEN (sizeof(SYNC))
#define REMOTE_ID_LEN (sizeof(REMOTE_ID))

#define CMD_CODE_LEN 2

// Command codes as byte arrays (MSB first, from original working bitstream)
const uint8_t QuietCool::speed_settings[][CMD_CODE_LEN] = {
    {0x66, 0x66}, // PRE
    {0xB1, 0xB1}, // H1
    {0xB2, 0xB2}, // H2
    {0xB4, 0xB4}, // H4
    {0xB8, 0xB8}, // H8
    {0xBC, 0xBC}, // H12
    {0xBF, 0xBF}, // HON
    {0xB0, 0xB0}, // HOFF
    {0xA1, 0xA1}, // M1
    {0xA2, 0xA2}, // M2
    {0xA4, 0xA4}, // M4
    {0xA8, 0xA8}, // M8
    {0xAC, 0xAC}, // M12
    {0xAF, 0xAF}, // MON
    {0xA0, 0xA0}, // MOFF
    {0x91, 0x91}, // L1
    {0x92, 0x92}, // L2
    {0x94, 0x94}, // L4
    {0x98, 0x98}, // L8
    {0x9C, 0x9C}, // L12
    {0x9F, 0x9F}, // LON
    {0x90, 0x90}, // LOFF
};

// Helper: Convert bytes to a bit string (MSB first)
static void bytesToBitString(const uint8_t* data, size_t len, char* bitstr, size_t maxlen) {
    size_t idx = 0;
    for (size_t i = 0; i < len && idx + 8 < maxlen; i++) {
        for (int b = 7; b >= 0; b--) {
            bitstr[idx++] = ((data[i] >> b) & 1) ? '1' : '0';
        }
    }
    bitstr[idx] = '\0';
}

// Helper: Log the bits in a byte array (MSB first)
void QuietCool::logBits(const uint8_t* data, size_t len) {
    char bitstr[8 * 32 + 1]; // up to 32 bytes (256 bits)
    bytesToBitString(data, len, bitstr, sizeof(bitstr));
    ESP_LOGD(TAG, "Bits sent: %s", bitstr);
}

void QuietCool::sendBitsFromBytes(const uint8_t *bytes, size_t byte_len) {
    for (size_t i = 0; i < byte_len; i++) {
        uint8_t value = bytes[i];
        for (int b = 7; b >= 0; b--) {
            digitalWrite(gdo0_pin, (value >> b) & 1);
            delayMicroseconds(415);
        }
    }
}

void QuietCool::sendRawData(const uint8_t* data, size_t len) {
    if (len == 0) {
        ESP_LOGE(TAG, "No data to send");
        return;
    }
    ESP_LOGD(TAG, "Sending %zu bytes (%zu bits)", len, len * 8);
    logBits(data, len);
    digitalWrite(gdo0_pin, 0);
    noInterrupts();
    ELECHOUSE_cc1101.SetTx();
    sendBitsFromBytes(data, len); // MSB first per byte
    ELECHOUSE_cc1101.setSidle();
    interrupts();
    delay(10);
}

void QuietCool::sendPacket(const uint8_t *cmd_code) {
    uint8_t full_cmd[SYNC_LEN + REMOTE_ID_LEN + CMD_CODE_LEN];
    memcpy(full_cmd, SYNC, SYNC_LEN);
    memcpy(full_cmd + SYNC_LEN, REMOTE_ID, REMOTE_ID_LEN);
    memcpy(full_cmd + SYNC_LEN + REMOTE_ID_LEN, cmd_code, CMD_CODE_LEN);
    size_t total_len = SYNC_LEN + REMOTE_ID_LEN + CMD_CODE_LEN;
    for (int i = 0; i < 3; i++) {
        sendRawData(full_cmd, total_len);
        delay(18);
    }
}

const uint8_t* QuietCool::getCommand(QuietCoolSpeed speed, QuietCoolDuration duration) {
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

uint8_t QuietCool::readChipVersion() {
    return ELECHOUSE_cc1101.SpiReadReg(0xF1);
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
    const uint8_t* cmd_code = getCommand(speed, duration);
    ESP_LOGI(TAG, "%02x %02x", cmd_code[0], cmd_code[1]);
    if (cmd_code)
        sendPacket(cmd_code);
}

}  // namespace quiet_cool
}  // namespace esphome 
