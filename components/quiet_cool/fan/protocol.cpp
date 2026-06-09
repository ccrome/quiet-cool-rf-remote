#include "protocol.h"
#include <cstring>

namespace esphome {
namespace quiet_cool {

bool is_valid_speed(uint8_t speed) {
    return speed == QUIETCOOL_SPEED_LOW || speed == QUIETCOOL_SPEED_MEDIUM ||
           speed == QUIETCOOL_SPEED_HIGH;
}

bool is_valid_duration(uint8_t duration) {
    switch (duration) {
    case QUIETCOOL_DURATION_OFF:
    case QUIETCOOL_DURATION_1H:
    case QUIETCOOL_DURATION_2H:
    case QUIETCOOL_DURATION_4H:
    case QUIETCOOL_DURATION_8H:
    case QUIETCOOL_DURATION_12H:
    case QUIETCOOL_DURATION_ON:
        return true;
    default:
        return false;
    }
}

uint8_t make_command(QuietCoolSpeed speed, QuietCoolDuration duration) {
    if (duration == QUIETCOOL_DURATION_OFF)
        return CMD_OFF;
    if (!is_valid_speed(speed) || !is_valid_duration(duration))
        return CMD_OFF;
    return static_cast<uint8_t>(speed) | static_cast<uint8_t>(duration);
}

void build_packet(const RemoteId &id, uint8_t cmd, uint8_t out[PACKET_LEN]) {
    memset(out, 0, PACKET_LEN);
    memset(out, 0xAA, ID_OFFSET);
    memcpy(out + ID_OFFSET, id.data(), id.size());
    out[CMD_OFFSET] = cmd;
    out[CMD_OFFSET + 1] = cmd;
}

// A command byte is plausible if it's WAKE, OFF, or a valid speed|duration combo
// with a non-OFF duration (speed|0x00 "PREP" frames also occur and mean OFF).
static bool is_known_command(uint8_t cmd) {
    if (cmd == CMD_WAKE || cmd == CMD_OFF)
        return true;
    return is_valid_speed(cmd & 0xF0) && is_valid_duration(cmd & 0x0F);
}

static DecodeResult decode_command(uint8_t cmd1, uint8_t cmd2, RxCommand &out);

DecodeResult decode_packet(const uint8_t *packet, size_t len, const RemoteId &id,
                           RxCommand &out) {
    out = RxCommand{};
    if (packet == nullptr || len < PACKET_LEN)
        return DecodeResult::TOO_SHORT;

    if (memcmp(packet + ID_OFFSET, id.data(), id.size()) != 0)
        return DecodeResult::ID_MISMATCH;

    return decode_command(packet[CMD_OFFSET], packet[CMD_OFFSET + 1], out);
}

DecodeResult decode_any(const uint8_t *packet, size_t len, RemoteId &sender,
                        RxCommand &out) {
    out = RxCommand{};
    if (packet == nullptr || len < PACKET_LEN)
        return DecodeResult::TOO_SHORT;

    // Without a known ID to match, require the 0xAA fill so arbitrary noise
    // can't masquerade as a packet from a new remote.
    for (size_t i = 0; i < ID_OFFSET; i++) {
        if (packet[i] != 0xAA)
            return DecodeResult::ID_MISMATCH;
    }

    memcpy(sender.data(), packet + ID_OFFSET, sender.size());
    return decode_command(packet[CMD_OFFSET], packet[CMD_OFFSET + 1], out);
}

static DecodeResult decode_command(uint8_t cmd1, uint8_t cmd2, RxCommand &out) {
    if (cmd1 != cmd2)
        return DecodeResult::CORRUPT_MISMATCH;

    DecodeResult result = DecodeResult::OK;
    if (!is_known_command(cmd1)) {
        // Known bit-sync corruption pattern: the command's MSB gets cleared
        // (e.g. 0x9F received as 0x1F). Restoring bit 7 recovers the original.
        // Only speed commands qualify — correcting 0x00 to OFF would let
        // zero-filled noise decode as a state change.
        uint8_t corrected = cmd1 | 0x80;
        if (!is_valid_speed(corrected & 0xF0) || !is_valid_duration(corrected & 0x0F))
            return DecodeResult::INVALID_COMMAND;
        cmd1 = corrected;
        result = DecodeResult::OK_CORRECTED;
    }

    out.valid = true;
    if (cmd1 == CMD_WAKE) {
        out.is_wake = true;
    } else if (cmd1 == CMD_OFF || (cmd1 & 0x0F) == QUIETCOOL_DURATION_OFF) {
        // speed|0x00 "PREP" frames (e.g. 0x90) report the fan as OFF
        out.is_off = true;
    } else {
        out.speed = cmd1 & 0xF0;
        out.duration = cmd1 & 0x0F;
    }
    return result;
}

bool extract_remote_id(const uint8_t *packet, size_t len, RemoteId &out) {
    if (packet == nullptr || len < PACKET_LEN)
        return false;

    // Require the 0xAA fill so noise can't pair: real packets always carry it.
    for (size_t i = 0; i < ID_OFFSET; i++) {
        if (packet[i] != 0xAA)
            return false;
    }

    uint8_t cmd1 = packet[CMD_OFFSET];
    uint8_t cmd2 = packet[CMD_OFFSET + 1];
    if (cmd1 != cmd2 || !is_known_command(cmd1))
        return false;

    memcpy(out.data(), packet + ID_OFFSET, out.size());
    return true;
}

uint8_t speed_level_to_cmd(int level, int speed_count) {
    if (speed_count == 2)
        return level <= 1 ? QUIETCOOL_SPEED_LOW : QUIETCOOL_SPEED_HIGH;
    if (level <= 1)
        return QUIETCOOL_SPEED_LOW;
    if (level == 2)
        return QUIETCOOL_SPEED_MEDIUM;
    return QUIETCOOL_SPEED_HIGH;
}

int cmd_to_speed_level(uint8_t cmd_speed, int speed_count) {
    if (cmd_speed == QUIETCOOL_SPEED_LOW)
        return 1;
    if (speed_count == 2)
        return 2;  // MEDIUM and HIGH both map to the top level
    return cmd_speed == QUIETCOOL_SPEED_MEDIUM ? 2 : 3;
}

// Frequency word: f_carrier = (f_xosc / 2^16) * FREQ, with f_xosc = 26 MHz.
// Kept as the ELECHOUSE successive-subtraction algorithm so register output is
// bit-identical to the configuration this fan was tuned with.
void freq_to_registers(float mhz, uint8_t &freq2, uint8_t &freq1, uint8_t &freq0) {
    freq2 = 0;
    freq1 = 0;
    freq0 = 0;
    for (bool done = false; !done;) {
        if (mhz >= 26) {
            mhz -= 26;
            freq2 += 1;
        } else if (mhz >= 0.1015625f) {
            mhz -= 0.1015625f;
            freq1 += 1;
        } else if (mhz >= 0.00039675f) {
            mhz -= 0.00039675f;
            freq0 += 1;
        } else {
            done = true;
        }
    }
}

// DEVIATN register: exponent (bits 6:4) and mantissa (bits 2:0), step table
// per CC1101 datasheet section 16.1. Ported from ELECHOUSE setDeviation().
uint8_t deviation_to_register(float khz) {
    float f = 1.586914f;
    float v = 0.19836425f;
    int c = 0;
    if (khz > 380.859375f)
        khz = 380.859375f;
    if (khz < 1.586914f)
        khz = 1.586914f;
    for (int i = 0; i < 255; i++) {
        f += v;
        if (c == 7) {
            v *= 2;
            c = -1;
            i += 8;
        }
        if (f >= khz) {
            c = i;
            i = 255;
        }
        c++;
    }
    return static_cast<uint8_t>(c);
}

uint8_t freq_offset_register(float mhz) {
    // ELECHOUSE: map(MHz, 378, 464, 31, 38) — Arduino map() takes longs, so
    // the frequency truncates to a whole MHz and the result uses integer math.
    long x = static_cast<long>(mhz);
    return static_cast<uint8_t>((x - 378) * (38 - 31) / (464 - 378) + 31);
}

}  // namespace quiet_cool
}  // namespace esphome
