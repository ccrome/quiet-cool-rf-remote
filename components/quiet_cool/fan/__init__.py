import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, output, spi
from esphome.const import (
    CONF_DIRECTION_OUTPUT,
    CONF_OSCILLATION_OUTPUT,
    CONF_OUTPUT,
    CONF_OUTPUT_ID,
)
from .. import quiet_cool_ns

# Additional pin configuration keys
CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_REMOTE_ID = "remote_id"
CONF_FREQ_MHZ = "center_freq_mhz"
CONF_DEVIATION_KHZ = "deviation_khz"
CONF_PREAMBLE_BYTE = "preamble_byte"
CONF_FSCTRL0_OVERRIDE = "fsctrl0_override"
CONF_VERIFY_ACK = "verify_ack"
CONF_ACK_TIMEOUT_MS = "ack_timeout_ms"
CONF_MAX_SEND_ATTEMPTS = "max_send_attempts"
CONF_SCK_PIN = "sck_pin"
CONF_MISO_PIN = "miso_pin"
CONF_MOSI_PIN = "mosi_pin"

DEPENDENCIES = ["spi"]

QuietCoolFan = quiet_cool_ns.class_("QuietCoolFan", cg.Component, fan.Fan, spi.SPIDevice)

CONFIG_SCHEMA = fan.FAN_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuietCoolFan),
        cv.Required(CONF_GDO0_PIN                      ): cv.uint8_t,
        cv.Required(CONF_GDO2_PIN                      ): cv.uint8_t,
        cv.Required(CONF_REMOTE_ID                     ): cv.ensure_list(cv.hex_uint8_t),
        cv.Optional(CONF_FREQ_MHZ     , default=433.897): cv.float_,
        cv.Optional(CONF_DEVIATION_KHZ, default=10.0   ): cv.float_,
        # Preamble byte after the leading 0x15. 0xAA (default) matches the
        # originally reverse-engineered protocol. Some hubs (notably Stealth Pro 7.0
        # with current firmware) reject 0xAA and require 0x55 — see PR notes.
        cv.Optional(CONF_PREAMBLE_BYTE, default=0xAA   ): cv.hex_uint8_t,
        # Optional override for CC1101 FSCTRL0 (frequency synth offset, reg 0x0C).
        # ELECHOUSE's Calibrate() (called from setMHZ) writes a frequency-mapped
        # value here — at 433 MHz that works out to ~35 (~+55 kHz offset, since
        # each unit is f_XOSC/2^14 ≈ 1.587 kHz). The register is 8-bit two's
        # complement, so values 0x00..0x7F shift TX up and 0x80..0xFF shift TX
        # down. Common uses: 0x00 to cancel the library's offset; a small non-zero
        # value to dial in per-unit crystal drift. Leave unset to keep the
        # library's behavior.
        cv.Optional(CONF_FSCTRL0_OVERRIDE              ): cv.hex_uint8_t,
        # Bidirectional protocol: when enabled, after every transmit the radio
        # switches to RX, listens for the hub's ACK packet, and retries the
        # send up to max_send_attempts if no ACK arrives within ack_timeout_ms.
        # Defaults preserve the current one-shot (no-ACK) behavior.
        cv.Optional(CONF_VERIFY_ACK, default=False   ): cv.boolean,
        cv.Optional(CONF_ACK_TIMEOUT_MS, default=800 ): cv.positive_int,
        cv.Optional(CONF_MAX_SEND_ATTEMPTS, default=5): cv.int_range(min=1, max=32),
        # Optional SPI pin overrides. Defaults (18/19/23) match the ESP32 VSPI
        # pins used on ccrome's original dev board. Set these for other boards,
        # e.g. LILYGO T-Embed CC1101 uses sck_pin=11, miso_pin=10, mosi_pin=9.
        cv.Optional(CONF_SCK_PIN, default=18 ): cv.uint8_t,
        cv.Optional(CONF_MISO_PIN, default=19): cv.uint8_t,
        cv.Optional(CONF_MOSI_PIN, default=23): cv.uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=True))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])  # type: QuietCoolFan
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    await spi.register_spi_device(var, config)

    cs_num = config[spi.CONF_CS_PIN]["number"]
    cg.add(var.set_pins(cs_num, config[CONF_GDO0_PIN], config[CONF_GDO2_PIN]))
    cg.add(var.set_remote_id(config[CONF_REMOTE_ID]))
    cg.add(var.set_frequencies(config[CONF_FREQ_MHZ], config[CONF_DEVIATION_KHZ]))
    cg.add(var.set_preamble_byte(config[CONF_PREAMBLE_BYTE]))
    if CONF_FSCTRL0_OVERRIDE in config:
        cg.add(var.set_fsctrl0_override(config[CONF_FSCTRL0_OVERRIDE]))
    cg.add(var.set_verify_ack(config[CONF_VERIFY_ACK]))
    cg.add(var.set_ack_timeout_ms(config[CONF_ACK_TIMEOUT_MS]))
    cg.add(var.set_max_send_attempts(config[CONF_MAX_SEND_ATTEMPTS]))
    cg.add(var.set_spi_pins(config[CONF_SCK_PIN], config[CONF_MISO_PIN], config[CONF_MOSI_PIN]))
