#include "catch_amalgamated.hpp"
#include "../components/quiet_cool/fan/protocol.h"

using namespace esphome::quiet_cool;

TEST_CASE("HA speed level to hardware speed, 2-speed fan", "[mapping]") {
    CHECK(speed_level_to_cmd(1, 2) == QUIETCOOL_SPEED_LOW);
    CHECK(speed_level_to_cmd(2, 2) == QUIETCOOL_SPEED_HIGH);
}

TEST_CASE("HA speed level to hardware speed, 3-speed fan", "[mapping]") {
    CHECK(speed_level_to_cmd(1, 3) == QUIETCOOL_SPEED_LOW);
    CHECK(speed_level_to_cmd(2, 3) == QUIETCOOL_SPEED_MEDIUM);
    CHECK(speed_level_to_cmd(3, 3) == QUIETCOOL_SPEED_HIGH);
}

TEST_CASE("hardware speed to HA level, 2-speed fan", "[mapping]") {
    CHECK(cmd_to_speed_level(QUIETCOOL_SPEED_LOW, 2) == 1);
    CHECK(cmd_to_speed_level(QUIETCOOL_SPEED_HIGH, 2) == 2);
    // A 2-speed fan has no MEDIUM, but a paired 3-speed remote could send one;
    // it maps to the top level.
    CHECK(cmd_to_speed_level(QUIETCOOL_SPEED_MEDIUM, 2) == 2);
}

TEST_CASE("hardware speed to HA level, 3-speed fan", "[mapping]") {
    CHECK(cmd_to_speed_level(QUIETCOOL_SPEED_LOW, 3) == 1);
    CHECK(cmd_to_speed_level(QUIETCOOL_SPEED_MEDIUM, 3) == 2);
    CHECK(cmd_to_speed_level(QUIETCOOL_SPEED_HIGH, 3) == 3);
}

TEST_CASE("mapping round-trips for every level", "[mapping]") {
    for (int count : {2, 3}) {
        for (int level = 1; level <= count; level++) {
            CHECK(cmd_to_speed_level(speed_level_to_cmd(level, count), count) == level);
        }
    }
}
