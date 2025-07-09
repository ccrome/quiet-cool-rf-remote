#include "quietcool.h"
#include "esphome/core/log.h"
#include "ELECHOUSE_CC1101_SRC_DRV.h"

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

void QuietCool::sendPacket(const uint8_t cmd_code) {
    uint8_t full_cmd[SYNC_LEN + 7 + CMD_CODE_LEN];
    memcpy(full_cmd, SYNC, SYNC_LEN);
    memcpy(full_cmd + SYNC_LEN, remote_id, 7);
    memcpy(full_cmd + SYNC_LEN + 7, &cmd_code, 1);
    memcpy(full_cmd + SYNC_LEN + 8, &cmd_code, 1);
    size_t total_len = SYNC_LEN + 7 + CMD_CODE_LEN;
    for (int i = 0; i < 3; i++) {
        sendRawData(full_cmd, total_len);
        delay(18);
    }
}

const uint8_t QuietCool::getCommand(QuietCoolSpeed speed, QuietCoolDuration duration) {
    const uint8_t off = QUIETCOOL_DURATION_OFF | QUIETCOOL_SPEED_LOW;
    switch (speed) {
    case QUIETCOOL_SPEED_HIGH:
    case QUIETCOOL_SPEED_MEDIUM:
    case QUIETCOOL_SPEED_LOW:
    break;
    default: return off;
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
    default: return off;
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
    const uint8_t cmd_code = getCommand(speed, duration);
    ESP_LOGI(TAG, "cmd=%02x ", cmd_code);
    sendPacket(cmd_code);
}

}  // namespace quiet_cool
}  // namespace esphome 
