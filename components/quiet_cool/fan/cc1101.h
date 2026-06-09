#pragma once
// Minimal CC1101 driver for the QuietCool protocol, talking through an
// abstract SPI interface (implemented by the component over ESPHome's SPI bus).
// Replaces the bundled ELECHOUSE driver; register values are kept bit-identical
// to the configuration the fan was paired with (see configure()).
#include <stdint.h>
#include <stddef.h>

namespace esphome {
namespace quiet_cool {

// Config registers
static constexpr uint8_t CC1101_IOCFG2 = 0x00;
static constexpr uint8_t CC1101_IOCFG0 = 0x02;
static constexpr uint8_t CC1101_SYNC1 = 0x04;
static constexpr uint8_t CC1101_SYNC0 = 0x05;
static constexpr uint8_t CC1101_PKTLEN = 0x06;
static constexpr uint8_t CC1101_PKTCTRL1 = 0x07;
static constexpr uint8_t CC1101_PKTCTRL0 = 0x08;
static constexpr uint8_t CC1101_ADDR = 0x09;
static constexpr uint8_t CC1101_CHANNR = 0x0A;
static constexpr uint8_t CC1101_FSCTRL1 = 0x0B;
static constexpr uint8_t CC1101_FSCTRL0 = 0x0C;
static constexpr uint8_t CC1101_FREQ2 = 0x0D;
static constexpr uint8_t CC1101_FREQ1 = 0x0E;
static constexpr uint8_t CC1101_FREQ0 = 0x0F;
static constexpr uint8_t CC1101_MDMCFG4 = 0x10;
static constexpr uint8_t CC1101_MDMCFG3 = 0x11;
static constexpr uint8_t CC1101_MDMCFG2 = 0x12;
static constexpr uint8_t CC1101_MDMCFG1 = 0x13;
static constexpr uint8_t CC1101_MDMCFG0 = 0x14;
static constexpr uint8_t CC1101_DEVIATN = 0x15;
static constexpr uint8_t CC1101_MCSM1 = 0x17;
static constexpr uint8_t CC1101_MCSM0 = 0x18;
static constexpr uint8_t CC1101_FOCCFG = 0x19;
static constexpr uint8_t CC1101_BSCFG = 0x1A;
static constexpr uint8_t CC1101_AGCCTRL2 = 0x1B;
static constexpr uint8_t CC1101_AGCCTRL1 = 0x1C;
static constexpr uint8_t CC1101_AGCCTRL0 = 0x1D;
static constexpr uint8_t CC1101_FREND1 = 0x21;
static constexpr uint8_t CC1101_FREND0 = 0x22;
static constexpr uint8_t CC1101_FSCAL3 = 0x23;
static constexpr uint8_t CC1101_FSCAL2 = 0x24;
static constexpr uint8_t CC1101_FSCAL1 = 0x25;
static constexpr uint8_t CC1101_FSCAL0 = 0x26;
static constexpr uint8_t CC1101_FSTEST = 0x29;
static constexpr uint8_t CC1101_TEST2 = 0x2C;
static constexpr uint8_t CC1101_TEST1 = 0x2D;
static constexpr uint8_t CC1101_TEST0 = 0x2E;

// Strobes
static constexpr uint8_t CC1101_SRES = 0x30;
static constexpr uint8_t CC1101_SCAL = 0x33;
static constexpr uint8_t CC1101_SRX = 0x34;
static constexpr uint8_t CC1101_STX = 0x35;
static constexpr uint8_t CC1101_SIDLE = 0x36;
static constexpr uint8_t CC1101_SFRX = 0x3A;
static constexpr uint8_t CC1101_SFTX = 0x3B;

// Status registers (read with the burst flag)
static constexpr uint8_t CC1101_PARTNUM = 0x30;
static constexpr uint8_t CC1101_VERSION = 0x31;
static constexpr uint8_t CC1101_MARCSTATE = 0x35;
static constexpr uint8_t CC1101_TXBYTES = 0x3A;
static constexpr uint8_t CC1101_RXBYTES = 0x3B;

static constexpr uint8_t CC1101_PATABLE = 0x3E;
static constexpr uint8_t CC1101_FIFO = 0x3F;

// MARCSTATE values
static constexpr uint8_t MARCSTATE_IDLE = 0x01;
static constexpr uint8_t MARCSTATE_RX = 0x0D;
static constexpr uint8_t MARCSTATE_RXFIFO_OVERFLOW = 0x11;
static constexpr uint8_t MARCSTATE_TXFIFO_UNDERFLOW = 0x16;

// SPI access provided by the owning component (ESPHome SPIDevice). One
// begin()/end() pair brackets each register access; CS is held low throughout.
class CC1101Spi {
  public:
    virtual ~CC1101Spi() = default;
    virtual void begin() = 0;
    virtual uint8_t transfer(uint8_t value) = 0;
    virtual void end() = 0;
};

class CC1101 {
  public:
    explicit CC1101(CC1101Spi *spi) : spi_(spi) {}

    // Datasheet 19.1.2 reset + chip presence check via VERSION register.
    bool reset_and_verify();

    // Full register configuration for the QuietCool protocol: 2-FSK,
    // 2.399 kBaud, 58 kHz RxBW, sync 0x15AA (15/16 match), fixed 20-byte
    // packets, status append, stay-in-RX, auto-calibration on IDLE->RX/TX.
    void configure(float freq_mhz, float deviation_khz);

    void set_frequency(float freq_mhz);

    uint8_t read_reg(uint8_t addr);
    void write_reg(uint8_t addr, uint8_t value);
    uint8_t read_status(uint8_t addr);
    void read_burst(uint8_t addr, uint8_t *buffer, uint8_t len);
    void write_burst(uint8_t addr, const uint8_t *buffer, uint8_t len);
    void strobe(uint8_t cmd);

    uint8_t marcstate() { return read_status(CC1101_MARCSTATE); }
    // Raw RXBYTES: bit 7 = overflow flag, bits 6:0 = byte count.
    uint8_t rx_bytes() { return read_status(CC1101_RXBYTES); }

    // State transitions confirmed via MARCSTATE, bounded at ~5 ms.
    bool go_idle();
    bool go_rx();
    // Full recovery: IDLE, flush both FIFOs, back to RX.
    bool recover_to_rx();

    void flush_rx() { strobe(CC1101_SFRX); }
    void flush_tx() { strobe(CC1101_SFTX); }

    // Manual frequency-synthesizer calibration (periodic drift compensation).
    void calibrate();

  private:
    bool wait_marcstate_(uint8_t target);
    CC1101Spi *spi_;
};

}  // namespace quiet_cool
}  // namespace esphome
