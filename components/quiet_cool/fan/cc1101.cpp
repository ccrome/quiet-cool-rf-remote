#include "cc1101.h"
#include "protocol.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace quiet_cool {

static const char *TAG = "quietcool.cc1101";

static constexpr uint8_t READ_SINGLE = 0x80;
static constexpr uint8_t READ_BURST = 0xC0;
static constexpr uint8_t WRITE_BURST = 0x40;

uint8_t CC1101::read_reg(uint8_t addr) {
    spi_->begin();
    spi_->transfer(addr | READ_SINGLE);
    uint8_t value = spi_->transfer(0);
    spi_->end();
    return value;
}

void CC1101::write_reg(uint8_t addr, uint8_t value) {
    spi_->begin();
    spi_->transfer(addr);
    spi_->transfer(value);
    spi_->end();
}

uint8_t CC1101::read_status(uint8_t addr) {
    spi_->begin();
    spi_->transfer(addr | READ_BURST);
    uint8_t value = spi_->transfer(0);
    spi_->end();
    return value;
}

void CC1101::read_burst(uint8_t addr, uint8_t *buffer, uint8_t len) {
    spi_->begin();
    spi_->transfer(addr | READ_BURST);
    for (uint8_t i = 0; i < len; i++)
        buffer[i] = spi_->transfer(0);
    spi_->end();
}

void CC1101::write_burst(uint8_t addr, const uint8_t *buffer, uint8_t len) {
    spi_->begin();
    spi_->transfer(addr | WRITE_BURST);
    for (uint8_t i = 0; i < len; i++)
        spi_->transfer(buffer[i]);
    spi_->end();
}

void CC1101::strobe(uint8_t cmd) {
    spi_->begin();
    spi_->transfer(cmd);
    spi_->end();
}

bool CC1101::reset_and_verify() {
    // SRES while CS is held low; the chip needs ~40us before the strobe.
    delayMicroseconds(50);
    strobe(CC1101_SRES);
    delay(5);

    for (int tries = 0; tries < 10; tries++) {
        uint8_t version = read_status(CC1101_VERSION);
        if (version == 0x14 || version == 0x04) {
            ESP_LOGI(TAG, "CC1101 detected (VERSION=0x%02X)", version);
            return true;
        }
        ESP_LOGW(TAG, "Unexpected CC1101 VERSION=0x%02X, retrying", version);
        delay(10);
    }
    return false;
}

void CC1101::configure(float freq_mhz, float deviation_khz) {
    // Values are bit-identical to the previous ELECHOUSE-based configuration
    // (verified by register readback on hardware); the fan was paired with
    // this exact setup. See RX_DEBUG_LOG.md for the tuning history.
    write_reg(CC1101_IOCFG2, 0x0B);    // (unused GDO2)
    write_reg(CC1101_IOCFG0, 0x06);    // GDO0: asserts on sync word, deasserts at packet end
    write_reg(CC1101_FSCTRL1, 0x06);   // IF = 152 kHz
    write_reg(CC1101_MDMCFG4, 0xF6);   // RxBW 58 kHz, DRATE_E=6
    write_reg(CC1101_MDMCFG3, 0x83);   // DRATE_M -> 2.399 kBaud
    write_reg(CC1101_MDMCFG2, 0x01);   // 2-FSK, no Manchester, sync 15/16 bits
    write_reg(CC1101_MDMCFG1, 0x02);   // no FEC, 2 preamble bytes (TX only)
    write_reg(CC1101_MDMCFG0, 0xF8);
    write_reg(CC1101_CHANNR, 0x00);
    write_reg(CC1101_DEVIATN, deviation_to_register(deviation_khz));
    write_reg(CC1101_FREND1, 0x56);
    write_reg(CC1101_FREND0, 0x10);    // PA table index 0
    write_reg(CC1101_MCSM1, 0x3C);     // CCA always; stay in RX after packet; IDLE after TX
    write_reg(CC1101_MCSM0, 0x18);     // auto-calibrate on IDLE->RX/TX
    write_reg(CC1101_FOCCFG, 0x1D);    // wide AFC limit for burst re-sync
    write_reg(CC1101_BSCFG, 0x1C);
    write_reg(CC1101_AGCCTRL2, 0xC7);
    write_reg(CC1101_AGCCTRL1, 0x00);
    write_reg(CC1101_AGCCTRL0, 0xB2);
    write_reg(CC1101_FSCAL3, 0xE9);
    write_reg(CC1101_FSCAL2, 0x2A);
    write_reg(CC1101_FSCAL1, 0x00);
    write_reg(CC1101_FSCAL0, 0x1F);
    write_reg(CC1101_FSTEST, 0x59);
    write_reg(CC1101_TEST2, 0x81);
    write_reg(CC1101_TEST1, 0x35);
    write_reg(CC1101_TEST0, 0x09);

    write_reg(CC1101_SYNC1, 0x15);     // sync word matches the remote's preamble tail
    write_reg(CC1101_SYNC0, 0xAA);
    write_reg(CC1101_PKTCTRL1, 0x04);  // PQT=0, append RSSI/LQI status bytes
    write_reg(CC1101_PKTCTRL0, 0x00);  // fixed length, no CRC, no whitening
    write_reg(CC1101_ADDR, 0x00);
    write_reg(CC1101_PKTLEN, PACKET_LEN);

    set_frequency(freq_mhz);

    // +10 dBm at 433 MHz (matches the physical remote's TX power)
    const uint8_t pa_table[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0};
    write_burst(CC1101_PATABLE, pa_table, sizeof(pa_table));
}

void CC1101::set_frequency(float freq_mhz) {
    uint8_t f2, f1, f0;
    freq_to_registers(freq_mhz, f2, f1, f0);
    write_reg(CC1101_FREQ2, f2);
    write_reg(CC1101_FREQ1, f1);
    write_reg(CC1101_FREQ0, f0);
    // Preserve ELECHOUSE's empirical crystal compensation — the tuned
    // center_freq_mhz in user configs assumes this offset is active.
    write_reg(CC1101_FSCTRL0, freq_offset_register(freq_mhz));
    write_reg(CC1101_TEST0, freq_mhz >= 430.5f ? 0x09 : 0x0B);
    ESP_LOGD(TAG, "Frequency set: %.3f MHz (FREQ=%02X%02X%02X FSCTRL0=0x%02X)",
             freq_mhz, f2, f1, f0, freq_offset_register(freq_mhz));
}

bool CC1101::wait_marcstate_(uint8_t target) {
    // Worst-case transition is IDLE->RX with calibration: ~800us.
    for (int i = 0; i < 100; i++) {
        if (marcstate() == target)
            return true;
        delayMicroseconds(50);
    }
    ESP_LOGW(TAG, "Timeout waiting for MARCSTATE 0x%02X (stuck at 0x%02X)",
             target, marcstate());
    return false;
}

bool CC1101::go_idle() {
    strobe(CC1101_SIDLE);
    return wait_marcstate_(MARCSTATE_IDLE);
}

bool CC1101::go_rx() {
    strobe(CC1101_SRX);
    return wait_marcstate_(MARCSTATE_RX);
}

bool CC1101::recover_to_rx() {
    go_idle();
    flush_rx();
    flush_tx();
    return go_rx();
}

void CC1101::calibrate() {
    go_idle();
    strobe(CC1101_SCAL);
    delay(1);  // calibration takes ~720us
    go_rx();
}

}  // namespace quiet_cool
}  // namespace esphome
