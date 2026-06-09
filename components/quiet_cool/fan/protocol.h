#pragma once
// Pure QuietCool RF protocol logic: packet build/decode, speed mapping, and
// CC1101 register math. No Arduino/ESPHome/FreeRTOS dependencies so it can be
// compiled and unit-tested on the host (see tests/).
#include <stdint.h>
#include <stddef.h>
#include <array>

namespace esphome {
namespace quiet_cool {

using RemoteId = std::array<uint8_t, 7>;

// Packet layout (after the CC1101 strips the 0x15AA hardware sync word):
//   bytes 0-6   0xAA fill
//   bytes 7-13  remote ID
//   bytes 14-15 command, sent twice (the protocol's integrity check)
//   bytes 16-19 zero padding
static constexpr size_t PACKET_LEN = 20;
static constexpr size_t FRAME_LEN = 22;  // packet + RSSI/LQI status bytes
static constexpr size_t ID_OFFSET = 7;
static constexpr size_t CMD_OFFSET = 14;

static constexpr uint8_t CMD_WAKE = 0x66;
static constexpr uint8_t CMD_OFF = 0x80;

enum QuietCoolSpeed : uint8_t {
    QUIETCOOL_SPEED_LOW    = 0x90,
    QUIETCOOL_SPEED_MEDIUM = 0xA0,
    QUIETCOOL_SPEED_HIGH   = 0xB0,
};

enum QuietCoolDuration : uint8_t {
    QUIETCOOL_DURATION_OFF = 0x00,
    QUIETCOOL_DURATION_1H  = 0x01,
    QUIETCOOL_DURATION_2H  = 0x02,
    QUIETCOOL_DURATION_4H  = 0x04,
    QUIETCOOL_DURATION_8H  = 0x08,
    QUIETCOOL_DURATION_12H = 0x0C,
    QUIETCOOL_DURATION_ON  = 0x0F,
};

enum class DecodeResult : uint8_t {
    OK,                // valid command decoded
    OK_CORRECTED,      // valid after bit-7 corruption correction
    TOO_SHORT,         // buffer smaller than PACKET_LEN
    ID_MISMATCH,       // sender is not the expected remote (noise or other unit)
    CORRUPT_MISMATCH,  // the two command copies disagree
    INVALID_COMMAND,   // command copies agree but aren't a known command
};

struct RxCommand {
    bool valid = false;
    bool is_wake = false;
    bool is_off = false;
    uint8_t speed = 0;     // 0x90/0xA0/0xB0 when valid && !is_wake && !is_off
    uint8_t duration = 0;  // 0x01..0x0F when speed is set
};

bool is_valid_speed(uint8_t speed);
bool is_valid_duration(uint8_t duration);

// Speed (high nibble) | duration (low nibble); OFF duration is the special 0x80.
uint8_t make_command(QuietCoolSpeed speed, QuietCoolDuration duration);

// Fill `out` with the 20-byte TX packet for `cmd` (hardware adds the sync word).
void build_packet(const RemoteId &id, uint8_t cmd, uint8_t out[PACKET_LEN]);

DecodeResult decode_packet(const uint8_t *packet, size_t len, const RemoteId &id,
                           RxCommand &out);

// Decode without binding to a specific remote: returns the sender's ID in
// `sender`. Callers filter against their paired-remote list.
DecodeResult decode_any(const uint8_t *packet, size_t len, RemoteId &sender,
                        RxCommand &out);

// Pairing support: if the packet is structurally a QuietCool command (any sender),
// store its remote ID in `out` and return true.
bool extract_remote_id(const uint8_t *packet, size_t len, RemoteId &out);

// Map a Home Assistant speed level (1..speed_count) to a hardware speed byte.
// 2-speed fans use LOW/HIGH; 3-speed fans use LOW/MEDIUM/HIGH.
uint8_t speed_level_to_cmd(int level, int speed_count);

// Map a received hardware speed byte back to an HA speed level.
int cmd_to_speed_level(uint8_t cmd_speed, int speed_count);

// CC1101 register math (ported from the ELECHOUSE driver, kept bit-identical).
void freq_to_registers(float mhz, uint8_t &freq2, uint8_t &freq1, uint8_t &freq0);
uint8_t deviation_to_register(float khz);

// ELECHOUSE's empirical crystal-offset compensation (FSCTRL0). It shifts the
// carrier by reg * 26MHz/2^14 (~1.59 kHz/LSB) — the fan was paired with this
// offset active, so it must be preserved exactly. 433 MHz band only.
uint8_t freq_offset_register(float mhz);

}  // namespace quiet_cool
}  // namespace esphome
