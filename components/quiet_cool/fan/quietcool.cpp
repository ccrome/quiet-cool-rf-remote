#include <stdint.h>
typedef uint8_t byte;
#include "quietcool.h"
#include "esphome/core/log.h"
#include "ELECHOUSE_CC1101_SRC_DRV.h"
#include <cstring>

namespace esphome {
namespace quiet_cool {

static const char *TAG = "quietcool";

const uint8_t SYNC[] = {0x15, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
#define SYNC_LEN (sizeof(SYNC))
#define REMOTE_ID_LEN (sizeof(REMOTE_ID))

#define CMD_CODE_LEN 2

// Commands are structured like this:
// LOW: 0x90
// MED: 0xA0    
// HIGH:0xB0
//
//  1 hour: 0x01
//  2 hour: 0x02
//  4 hour: 0x04
//  8 hour: 0x08
// 12 hour: 0x0C
//      on: 0x0F
//     off: 0x00
//
// the special command that's sent before anything else is 0x66.

// So, final packet format is:
//     <---- SYNC -------><--- ID -----><CD>
// 0x: 150aaaaaaaaaaaaaaaaTTUUVVWWXXYYZZGGGG
// sync: always the same
//   ID: unique identifier
//   CD: command, as described above.  Two bytes duplicated.

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
    
    // Print bytes in hex format
    ESP_LOGD(TAG, "Bytes sent: ");
    for (size_t i = 0; i < len; i++) {
        ESP_LOGD(TAG, "0x%02X ", data[i]);
    }
    ESP_LOGD(TAG, "Bits sent: %s", bitstr);
}

void QuietCool::sendRawData(const uint8_t* data, size_t len) {
    if (len == 0) {
        ESP_LOGE(TAG, "No data to send");
        return;
    }
    ESP_LOGD(TAG, "Sending %zu bytes (%zu bits)", len, len * 8);
    logBits(data, len);
    ESP_LOGD(TAG, "[DEBUG] Before SendData");
    ELECHOUSE_cc1101.SendData((byte*)data, (byte)len);
    ESP_LOGD(TAG, "[DEBUG] After SendData");
    delay(10);
}

void QuietCool::sendPacket(const uint8_t cmd_code) {
    const uint8_t padding_len = 2;
    uint8_t full_cmd[SYNC_LEN + 7 + CMD_CODE_LEN+padding_len];
    memset(full_cmd, 0, sizeof(full_cmd));
    memcpy(full_cmd, SYNC, SYNC_LEN);
    memcpy(full_cmd + SYNC_LEN, remote_id, 7);
    memcpy(full_cmd + SYNC_LEN + 7, &cmd_code, 1);
    memcpy(full_cmd + SYNC_LEN + 8, &cmd_code, 1);
    size_t total_len = SYNC_LEN + 7 + CMD_CODE_LEN+padding_len;
    for (int i = 0; i < 3; i++) {
        sendRawData(full_cmd, total_len);
        delay(18);
    }
}

const uint8_t QuietCool::getCommand(QuietCoolSpeed speed, QuietCoolDuration duration) {
    ESP_LOGD(TAG, "getCommand got: speed=0x%02x, duration=0x%02x", speed, duration);
    const uint8_t off = QUIETCOOL_DURATION_OFF | QUIETCOOL_SPEED_LOW;
    switch (speed) {
    case QUIETCOOL_SPEED_HIGH:
    case QUIETCOOL_SPEED_MEDIUM:
    case QUIETCOOL_SPEED_LOW:
    break;
    default:
	ESP_LOGD(TAG, "unknown speed: 0x%02x", speed);
	return off;
    };

    switch (duration) {
    case QUIETCOOL_DURATION_1H  :
    case QUIETCOOL_DURATION_2H  :
    case QUIETCOOL_DURATION_4H  :
    case QUIETCOOL_DURATION_8H  :
    case QUIETCOOL_DURATION_12H :
    case QUIETCOOL_DURATION_ON  :
    case QUIETCOOL_DURATION_OFF :
    break;
    default:
	ESP_LOGD(TAG, "unknown duration: 0x%02x", duration);
	return off;
    }
    uint8_t result = speed | duration;
    ESP_LOGD(TAG, "Sending speed=0x%02x, duration=0x%02x: 0x%02x", speed, duration, result);
    return result;
}

QuietCool::QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi, const uint8_t* remote_id_in)
    : csn_pin(csn), gdo0_pin(gdo0), gdo2_pin(gdo2), sck_pin(sck), miso_pin(miso), mosi_pin(mosi) {
    for (int i = 0; i < 7; ++i) remote_id[i] = remote_id_in[i];
}

// --- Initialize CC1101 and verify communication ---
bool QuietCool::initCC1101() {
    ESP_LOGD(TAG, "sck:%d, miso:%d, mosi:%d, csn:%d, gdo0:%d\n", sck_pin, miso_pin, mosi_pin, csn_pin, gdo0_pin);
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

    // ✅ Must set CC mode BEFORE Init so registers are configured correctly
    ELECHOUSE_cc1101.setCCMode(1);
    ELECHOUSE_cc1101.Init();

    ESP_LOGI(TAG, "Setting GDO0 pin to %d", gdo0_pin);
    ELECHOUSE_cc1101.setGDO0(gdo0_pin);

    // Basic configuration
    ELECHOUSE_cc1101.setMHZ(FREQ_MHZ);
    ELECHOUSE_cc1101.setPA(0);

    // Packet-related configuration
    ELECHOUSE_cc1101.setModulation(0);       // FSK
    ELECHOUSE_cc1101.setDeviation(10);
    ELECHOUSE_cc1101.setDRate(2.398);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setWhiteData(false);
    ELECHOUSE_cc1101.setManchester(false);
    ELECHOUSE_cc1101.setPktFormat(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setLengthConfig(0);
    ELECHOUSE_cc1101.setPacketLength(20);
    ELECHOUSE_cc1101.setPRE(0);

    delay(500);
    return true;
}
uint8_t QuietCool::readChipVersion() {
    return ELECHOUSE_cc1101.SpiReadReg(0xF1);
}

void QuietCool::begin() {
    ESP_LOGI(TAG, "Starting CC1101 setup");
    if (!initCC1101()) {
        ESP_LOGE(TAG, "CC1101 not detected");
        return;
    }
    ESP_LOGI(TAG, "CC1101 ready");
}

void QuietCool::send(QuietCoolSpeed speed, QuietCoolDuration duration) {
    ESP_LOGI(TAG, "send(0x%02x, %0x%02x)", speed, duration);
    const uint8_t cmd_code = getCommand(speed, duration);
    ESP_LOGI(TAG, "cmd=%02x ", cmd_code);
    sendPacket(cmd_code);
}

}  // namespace quiet_cool
}  // namespace esphome 
