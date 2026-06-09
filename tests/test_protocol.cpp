#include "catch_amalgamated.hpp"
#include "../components/quiet_cool/fan/protocol.h"

#include <cstring>
#include <vector>

using namespace esphome::quiet_cool;

// The remote captured with an RTL-SDR for the original protocol analysis
// (bitstrings preserved in arduino/src/quietcool.cpp).
static const RemoteId CAPTURED_ID = {0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2};
static const RemoteId OTHER_ID = {0x2D, 0xD4, 0x06, 0xCB, 0x03, 0xE6, 0x45};

static std::vector<uint8_t> packet_for(const RemoteId &id, uint8_t cmd1, uint8_t cmd2) {
    std::vector<uint8_t> p(PACKET_LEN, 0);
    memset(p.data(), 0xAA, ID_OFFSET);
    memcpy(p.data() + ID_OFFSET, id.data(), id.size());
    p[CMD_OFFSET] = cmd1;
    p[CMD_OFFSET + 1] = cmd2;
    return p;
}

TEST_CASE("build_packet layout matches RF captures", "[protocol]") {
    // RF capture of "LON" (low speed, always on), hardware sync 0x15AA stripped:
    //   AA x7 fill, remote ID, 0x9F twice, zero padding to 20 bytes.
    const uint8_t expected[PACKET_LEN] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0x2D, 0xD4, 0x06, 0xCB, 0x00, 0xF7, 0xF2,
        0x9F, 0x9F, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t out[PACKET_LEN];
    build_packet(CAPTURED_ID, 0x9F, out);
    CHECK(memcmp(out, expected, PACKET_LEN) == 0);
}

TEST_CASE("command bytes match RF captures", "[protocol]") {
    // Command bytes decoded from the captured bitstrings.
    CHECK(make_command(QUIETCOOL_SPEED_HIGH, QUIETCOOL_DURATION_1H) == 0xB1);   // H1
    CHECK(make_command(QUIETCOOL_SPEED_HIGH, QUIETCOOL_DURATION_ON) == 0xBF);   // HON
    CHECK(make_command(QUIETCOOL_SPEED_MEDIUM, QUIETCOOL_DURATION_ON) == 0xAF); // MON
    CHECK(make_command(QUIETCOOL_SPEED_LOW, QUIETCOOL_DURATION_ON) == 0x9F);    // LON
    CHECK(make_command(QUIETCOOL_SPEED_LOW, QUIETCOOL_DURATION_8H) == 0x98);    // L8
    // OFF is the dedicated 0x80 command regardless of speed.
    CHECK(make_command(QUIETCOOL_SPEED_HIGH, QUIETCOOL_DURATION_OFF) == 0x80);
}

TEST_CASE("decode round-trips every speed and duration", "[protocol]") {
    const QuietCoolSpeed speeds[] = {QUIETCOOL_SPEED_LOW, QUIETCOOL_SPEED_MEDIUM,
                                     QUIETCOOL_SPEED_HIGH};
    const QuietCoolDuration durations[] = {
        QUIETCOOL_DURATION_1H, QUIETCOOL_DURATION_2H, QUIETCOOL_DURATION_4H,
        QUIETCOOL_DURATION_8H, QUIETCOOL_DURATION_12H, QUIETCOOL_DURATION_ON};

    for (auto spd : speeds) {
        for (auto dur : durations) {
            uint8_t cmd = make_command(spd, dur);
            uint8_t pkt[PACKET_LEN];
            build_packet(CAPTURED_ID, cmd, pkt);
            RxCommand rx;
            REQUIRE(decode_packet(pkt, PACKET_LEN, CAPTURED_ID, rx) == DecodeResult::OK);
            CHECK(rx.valid);
            CHECK_FALSE(rx.is_wake);
            CHECK_FALSE(rx.is_off);
            CHECK(rx.speed == static_cast<uint8_t>(spd));
            CHECK(rx.duration == static_cast<uint8_t>(dur));
        }
    }
}

TEST_CASE("decode WAKE and OFF", "[protocol]") {
    RxCommand rx;

    auto wake = packet_for(CAPTURED_ID, CMD_WAKE, CMD_WAKE);
    REQUIRE(decode_packet(wake.data(), wake.size(), CAPTURED_ID, rx) == DecodeResult::OK);
    CHECK(rx.valid);
    CHECK(rx.is_wake);
    CHECK_FALSE(rx.is_off);

    auto off = packet_for(CAPTURED_ID, CMD_OFF, CMD_OFF);
    REQUIRE(decode_packet(off.data(), off.size(), CAPTURED_ID, rx) == DecodeResult::OK);
    CHECK(rx.valid);
    CHECK(rx.is_off);

    // The remote's own OFF is speed|0x00 ("PREP", e.g. HOFF = 0xB0 from capture).
    auto hoff = packet_for(CAPTURED_ID, 0xB0, 0xB0);
    REQUIRE(decode_packet(hoff.data(), hoff.size(), CAPTURED_ID, rx) == DecodeResult::OK);
    CHECK(rx.valid);
    CHECK(rx.is_off);
    CHECK_FALSE(rx.is_wake);
}

TEST_CASE("mismatched command copies are rejected", "[protocol][regression]") {
    // Regression: the original loop decoded packets even after logging them as
    // corrupt. 0x9F/0x1F is the exact corruption documented in RX_DEBUG_LOG.md.
    auto pkt = packet_for(CAPTURED_ID, 0x9F, 0x1F);
    RxCommand rx;
    CHECK(decode_packet(pkt.data(), pkt.size(), CAPTURED_ID, rx) ==
          DecodeResult::CORRUPT_MISMATCH);
    CHECK_FALSE(rx.valid);
}

TEST_CASE("bit-7 corruption is corrected when both copies agree", "[protocol]") {
    // MSB cleared on both copies: 0x9F -> 0x1F.
    auto pkt = packet_for(CAPTURED_ID, 0x1F, 0x1F);
    RxCommand rx;
    REQUIRE(decode_packet(pkt.data(), pkt.size(), CAPTURED_ID, rx) ==
            DecodeResult::OK_CORRECTED);
    CHECK(rx.valid);
    CHECK(rx.speed == QUIETCOOL_SPEED_LOW);
    CHECK(rx.duration == QUIETCOOL_DURATION_ON);
}

TEST_CASE("uncorrectable commands are rejected", "[protocol]") {
    RxCommand rx;

    // 0x55 | 0x80 = 0xD5: not a valid speed nibble.
    auto pkt = packet_for(CAPTURED_ID, 0x55, 0x55);
    CHECK(decode_packet(pkt.data(), pkt.size(), CAPTURED_ID, rx) ==
          DecodeResult::INVALID_COMMAND);
    CHECK_FALSE(rx.valid);

    // Valid speed but invalid duration nibble (0x93 -> duration 0x03).
    pkt = packet_for(CAPTURED_ID, 0x93, 0x93);
    CHECK(decode_packet(pkt.data(), pkt.size(), CAPTURED_ID, rx) ==
          DecodeResult::INVALID_COMMAND);

    // All-zero command must not be "corrected" into OFF (0x80): zero-filled
    // noise would otherwise turn the fan off in HA.
    pkt = packet_for(CAPTURED_ID, 0x00, 0x00);
    CHECK(decode_packet(pkt.data(), pkt.size(), CAPTURED_ID, rx) ==
          DecodeResult::INVALID_COMMAND);
    CHECK_FALSE(rx.valid);
}

TEST_CASE("foreign and malformed packets are rejected", "[protocol]") {
    RxCommand rx;

    auto pkt = packet_for(OTHER_ID, 0x9F, 0x9F);
    CHECK(decode_packet(pkt.data(), pkt.size(), CAPTURED_ID, rx) ==
          DecodeResult::ID_MISMATCH);
    CHECK_FALSE(rx.valid);

    CHECK(decode_packet(pkt.data(), PACKET_LEN - 1, CAPTURED_ID, rx) ==
          DecodeResult::TOO_SHORT);
    CHECK(decode_packet(nullptr, PACKET_LEN, CAPTURED_ID, rx) ==
          DecodeResult::TOO_SHORT);
}

TEST_CASE("extract_remote_id accepts any sender with a valid packet", "[pairing]") {
    auto pkt = packet_for(OTHER_ID, 0x66, 0x66);
    RemoteId id;
    REQUIRE(extract_remote_id(pkt.data(), pkt.size(), id));
    CHECK(id == OTHER_ID);
}

TEST_CASE("extract_remote_id rejects noise", "[pairing]") {
    RemoteId id;

    // Missing 0xAA fill.
    std::vector<uint8_t> noise(PACKET_LEN, 0x37);
    CHECK_FALSE(extract_remote_id(noise.data(), noise.size(), id));

    // Good fill but mismatched command copies.
    auto pkt = packet_for(OTHER_ID, 0x9F, 0x66);
    CHECK_FALSE(extract_remote_id(pkt.data(), pkt.size(), id));

    // Good fill but unknown command.
    pkt = packet_for(OTHER_ID, 0x12, 0x12);
    CHECK_FALSE(extract_remote_id(pkt.data(), pkt.size(), id));

    CHECK_FALSE(extract_remote_id(pkt.data(), PACKET_LEN - 1, id));
}
