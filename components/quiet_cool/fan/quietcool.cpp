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

#define CMD_CODE_LEN 2

QuietCool *QuietCool::active_receiver_ = nullptr;

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
    ESP_LOGD(TAG, "Bits sent: %s", bitstr);

    bitstr[0] = 0;
    for (size_t i = 0; i < len; i++) {
	char bb[3];
	snprintf(bb, 3, "%02X", data[i]);
        strcat(bitstr, bb);
    }
    ESP_LOGD(TAG, "Bytes sent: %s", bitstr);
}

void QuietCool::sendRawData(const uint8_t* data, size_t len) {
    if (len == 0) {
        ESP_LOGE(TAG, "No data to send");
        return;
    }
    ESP_LOGD(TAG, "Sending %zu bytes (%zu bits)", len, len * 8);
    logBits(data, len);
    ELECHOUSE_cc1101.SendData((byte*)data, (byte)len);
    delay(10);
}

void QuietCool::configureTransmit() {
    // Preserve the working packet-mode transmitter. The captured QuietCool
    // preamble/sync bytes are part of the FIFO payload, not CC1101 framing.
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setLengthConfig(0);
    ELECHOUSE_cc1101.setPacketLength(20);
    ELECHOUSE_cc1101.setAppendStatus(false);
}

void QuietCool::configureReceive() {
    // Use the first two bytes of the configured remote ID as the hardware sync
    // word. The FIFO then contains the other five ID bytes and two command
    // bytes, allowing the CC1101 to establish both bit and byte alignment.
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setSyncWord(remote_id[0], remote_id[1]);
    ELECHOUSE_cc1101.setSyncMode(2);  // 16/16 sync word, no carrier-sense gate
    ELECHOUSE_cc1101.setWhiteData(false);
    ELECHOUSE_cc1101.setManchester(false);
    ELECHOUSE_cc1101.setPktFormat(0);
    ELECHOUSE_cc1101.setCrc(false);
    ELECHOUSE_cc1101.setLengthConfig(0);
    ELECHOUSE_cc1101.setPacketLength(RX_PAYLOAD_LEN);
    ELECHOUSE_cc1101.setPQT(0);
    ELECHOUSE_cc1101.setAdrChk(0);
    ELECHOUSE_cc1101.setAppendStatus(true);
}

void QuietCool::startListening() {
    if (!initialized_)
        return;

    listening_ = false;
    noInterrupts();
    packet_ready_ = false;
    interrupts();

    configureReceive();
    ELECHOUSE_cc1101.SpiStrobe(CC1101_SFRX);
    ELECHOUSE_cc1101.SetRx();
    listening_ = true;
    ESP_LOGD(TAG, "Listening for remote ID %02X:%02X:%02X:%02X:%02X:%02X:%02X",
             remote_id[0], remote_id[1], remote_id[2], remote_id[3],
             remote_id[4], remote_id[5], remote_id[6]);
}

void QuietCool::restartReceiver() {
    listening_ = false;
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.SpiStrobe(CC1101_SFRX);
    ELECHOUSE_cc1101.SetRx();
    listening_ = true;
}

void IRAM_ATTR QuietCool::handleGdo0Interrupt() {
    QuietCool *receiver = active_receiver_;
    if (receiver != nullptr && receiver->listening_)
        receiver->packet_ready_ = true;
}

int8_t QuietCool::decodeRssi(uint8_t raw_rssi) {
    const int16_t signed_rssi = raw_rssi >= 128 ? raw_rssi - 256 : raw_rssi;
    return static_cast<int8_t>(signed_rssi / 2 - 74);
}

bool QuietCool::decodePayload(const uint8_t *payload, QuietCoolCommand &command) const {
    if (memcmp(payload, remote_id + 2, REMOTE_ID_LEN - 2) != 0) {
        ESP_LOGV(TAG, "Ignoring frame with non-matching remote ID suffix");
        return false;
    }

    const uint8_t command_code = payload[REMOTE_ID_LEN - 2];
    const uint8_t repeated_code = payload[REMOTE_ID_LEN - 1];
    if (command_code != repeated_code) {
        ESP_LOGW(TAG, "Ignoring frame with mismatched command bytes: %02X != %02X",
                 command_code, repeated_code);
        return false;
    }

    const uint8_t speed_code = command_code & 0xF0;
    switch (speed_code) {
        case QUIETCOOL_SPEED_LOW:
        case QUIETCOOL_SPEED_MEDIUM:
        case QUIETCOOL_SPEED_HIGH:
            command.speed = static_cast<QuietCoolSpeed>(speed_code);
            break;
        default:
            // 0x66 is a setup/pre-command and does not represent fan state.
            ESP_LOGV(TAG, "Ignoring non-state command 0x%02X", command_code);
            return false;
    }

    const uint8_t duration_code = command_code & 0x0F;
    switch (duration_code) {
        case QUIETCOOL_DURATION_1H:
        case QUIETCOOL_DURATION_2H:
        case QUIETCOOL_DURATION_4H:
        case QUIETCOOL_DURATION_8H:
        case QUIETCOOL_DURATION_12H:
        case QUIETCOOL_DURATION_ON:
        case QUIETCOOL_DURATION_OFF:
            command.duration = static_cast<QuietCoolDuration>(duration_code);
            break;
        default:
            ESP_LOGW(TAG, "Ignoring command with invalid duration: 0x%02X", command_code);
            return false;
    }

    command.code = command_code;
    return true;
}

bool QuietCool::receive(QuietCoolCommand &command) {
    if (!initialized_ || !packet_ready_)
        return false;

    noInterrupts();
    packet_ready_ = false;
    interrupts();

    const uint8_t rx_bytes_status = ELECHOUSE_cc1101.SpiReadStatus(CC1101_RXBYTES);
    if ((rx_bytes_status & 0x80) != 0) {
        ESP_LOGW(TAG, "CC1101 RX FIFO overflow; restarting receiver");
        restartReceiver();
        return false;
    }

    const uint8_t available = rx_bytes_status & 0x7F;
    constexpr size_t expected = RX_PAYLOAD_LEN + RX_STATUS_LEN;
    if (available < expected) {
        ESP_LOGW(TAG, "Incomplete RX frame: got %u bytes, expected %u",
                 available, static_cast<unsigned>(expected));
        restartReceiver();
        return false;
    }

    uint8_t frame[expected];
    ELECHOUSE_cc1101.SpiReadBurstReg(CC1101_RXFIFO, frame, expected);
    restartReceiver();

    command.rssi_dbm = decodeRssi(frame[RX_PAYLOAD_LEN]);
    command.lqi = frame[RX_PAYLOAD_LEN + 1] & 0x7F;
    if (!decodePayload(frame, command))
        return false;

    const uint32_t now = millis();
    const bool duplicate = command.code == last_rx_code_ &&
                           static_cast<uint32_t>(now - last_rx_at_) < RX_DEDUP_WINDOW_MS;
    last_rx_code_ = command.code;
    last_rx_at_ = now;
    if (duplicate) {
        ESP_LOGV(TAG, "Ignoring repeated remote frame 0x%02X", command.code);
        return false;
    }

    ESP_LOGI(TAG, "Received remote command 0x%02X (RSSI %d dBm, LQI %u)",
             command.code, command.rssi_dbm, command.lqi);
    return true;
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
    const uint8_t off = static_cast<uint8_t>(QUIETCOOL_DURATION_OFF) |
                        static_cast<uint8_t>(QUIETCOOL_SPEED_LOW);
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
    uint8_t result = static_cast<uint8_t>(speed) | static_cast<uint8_t>(duration);
    ESP_LOGD(TAG, "Sending speed=0x%02x, duration=0x%02x: 0x%02x", speed, duration, result);
    return result;
}

    QuietCool::QuietCool(uint8_t csn, uint8_t gdo0, uint8_t gdo2, uint8_t sck, uint8_t miso, uint8_t mosi, const uint8_t* remote_id_in, float center_freq, float deviation_khz) : 
    csn_pin(csn),
    gdo0_pin(gdo0),
    gdo2_pin(gdo2),
    sck_pin(sck),
    miso_pin(miso),
    mosi_pin(mosi),
    center_freq_mhz(center_freq),
    deviation_khz(deviation_khz)
{
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

    ESP_LOGE(TAG, "Setting GDO0 pin to %d", gdo0_pin);
    ELECHOUSE_cc1101.setGDO0(gdo0_pin);

    // Basic configuration
    ESP_LOGE(TAG, "Setting center frequency to %f MHz", center_freq_mhz);
    ELECHOUSE_cc1101.setMHZ(center_freq_mhz);
    ELECHOUSE_cc1101.setPA(0);

    // Packet-related configuration
    ELECHOUSE_cc1101.setModulation(0);       // FSK
    ESP_LOGI(TAG, "Setting deviaion to %f kHz.  That's a total spread of %f kHz", deviation_khz, 2*deviation_khz);
    ELECHOUSE_cc1101.setDeviation(deviation_khz);
    ELECHOUSE_cc1101.setDRate(2.398);
    // Wide enough for this 2-FSK signal and typical module frequency error,
    // while substantially improving receive sensitivity over the reset maximum.
    ELECHOUSE_cc1101.setRxBW(100.0);
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
    initialized_ = true;
    active_receiver_ = this;
    attachInterrupt(digitalPinToInterrupt(gdo0_pin), QuietCool::handleGdo0Interrupt, FALLING);
    startListening();
    ESP_LOGI(TAG, "CC1101 ready");
}

void QuietCool::send(QuietCoolSpeed speed, QuietCoolDuration duration) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Cannot transmit before CC1101 initialization");
        return;
    }

    ESP_LOGI(TAG, "send(0x%02x, %0x%02x)", speed, duration);
    const uint8_t cmd_code = getCommand(speed, duration);
    ESP_LOGI(TAG, "cmd=%02x ", cmd_code);
    listening_ = false;
    noInterrupts();
    packet_ready_ = false;
    interrupts();
    configureTransmit();
    sendPacket(cmd_code);
    startListening();
}

}  // namespace quiet_cool
}  // namespace esphome 
