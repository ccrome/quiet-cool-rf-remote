#include "catch_amalgamated.hpp"
#include "../components/quiet_cool/fan/protocol.h"

#include <cstdint>
#include <cmath>

using namespace esphome::quiet_cool;

// Datasheet: f_carrier = (f_xosc / 2^16) * FREQ[23:0], f_xosc = 26 MHz.
static uint32_t freq_word(float mhz) {
    uint8_t f2, f1, f0;
    freq_to_registers(mhz, f2, f1, f0);
    return (uint32_t(f2) << 16) | (uint32_t(f1) << 8) | f0;
}

// Datasheet: f_dev = (f_xosc / 2^17) * (8 + mantissa) * 2^exponent.
static float register_to_deviation_khz(uint8_t reg) {
    int exponent = (reg >> 4) & 0x07;
    int mantissa = reg & 0x07;
    return (26000.0f / 131072.0f) * (8 + mantissa) * float(1 << exponent);
}

TEST_CASE("frequency registers for the project's tuned frequency", "[cc1101]") {
    // 433.875 MHz: every intermediate value is exactly representable, so the
    // registers are exact: FREQ = 433.875 * 2^16 / 26 = 0x10B000.
    uint8_t f2, f1, f0;
    freq_to_registers(433.875f, f2, f1, f0);
    CHECK(f2 == 0x10);
    CHECK(f1 == 0xB0);
    CHECK(f0 == 0x00);
}

TEST_CASE("frequency word tracks the datasheet formula", "[cc1101]") {
    for (float mhz : {433.92f, 433.897f, 433.85f, 433.95f, 315.0f, 868.3f}) {
        uint32_t expected = uint32_t(std::lround(double(mhz) * 65536.0 / 26.0));
        uint32_t actual = freq_word(mhz);
        INFO("mhz=" << mhz << " expected=" << expected << " actual=" << actual);
        // The successive-subtraction algorithm truncates rather than rounds;
        // allow 2 LSB (~800 Hz), far below the channel's tolerance.
        CHECK(std::abs(int64_t(actual) - int64_t(expected)) <= 2);
    }
}

TEST_CASE("deviation register for the project's 10 kHz setting", "[cc1101]") {
    uint8_t reg = deviation_to_register(10.0f);
    float actual_khz = register_to_deviation_khz(reg);
    INFO("reg=0x" << std::hex << int(reg) << " -> " << std::dec << actual_khz << " kHz");
    // The algorithm picks the first representable step >= the request;
    // for 10 kHz that's (8+5)*2^2 steps = 10.31 kHz, register 0x25.
    CHECK(reg == 0x25);
    CHECK(actual_khz >= 10.0f);
    CHECK(actual_khz <= 11.0f);
}

TEST_CASE("deviation register matches chip reset default for ~47 kHz", "[cc1101]") {
    // DEVIATN reset value 0x47 = (8+7)*2^4 steps = 47.607 kHz. Request a value
    // mid-step (the float-accumulation algorithm overshoots on exact bounds).
    CHECK(deviation_to_register(45.0f) == 0x47);
}

TEST_CASE("deviation requests are clamped to representable range", "[cc1101]") {
    CHECK(register_to_deviation_khz(deviation_to_register(0.5f)) ==
          register_to_deviation_khz(deviation_to_register(1.586914f)));
    CHECK(register_to_deviation_khz(deviation_to_register(500.0f)) ==
          register_to_deviation_khz(deviation_to_register(380.859375f)));
}
