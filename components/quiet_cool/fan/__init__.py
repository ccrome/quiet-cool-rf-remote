import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, spi
from esphome.const import CONF_OUTPUT_ID

from .. import quiet_cool_ns

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_REMOTE_ID = "remote_id"
CONF_FREQ_MHZ = "center_freq_mhz"
CONF_DEVIATION_KHZ = "deviation_khz"
CONF_SPEED_COUNT = "speed_count"

DEPENDENCIES = ["spi"]

QuietCoolFan = quiet_cool_ns.class_("QuietCoolFan", cg.Component, fan.Fan, spi.SPIDevice)


def _validate_remote_id(value):
    value = cv.ensure_list(cv.hex_uint8_t)(value)
    if len(value) != 7:
        raise cv.Invalid("remote_id must be exactly 7 bytes")
    return value


CONFIG_SCHEMA = (
    fan.fan_schema(QuietCoolFan)
    .extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuietCoolFan),
            cv.Required(CONF_GDO0_PIN): cv.uint8_t,
            cv.Required(CONF_GDO2_PIN): cv.uint8_t,
            # Optional: without it, the device enters pairing mode on first
            # boot and learns the ID from any remote button press.
            cv.Optional(CONF_REMOTE_ID): _validate_remote_id,
            cv.Optional(CONF_FREQ_MHZ, default=433.897): cv.float_,
            cv.Optional(CONF_DEVIATION_KHZ, default=10.0): cv.float_,
            cv.Optional(CONF_SPEED_COUNT, default=3): cv.int_range(min=2, max=3),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_pins(config[CONF_GDO0_PIN], config[CONF_GDO2_PIN]))
    if CONF_REMOTE_ID in config:
        cg.add(var.set_remote_id(config[CONF_REMOTE_ID]))
    cg.add(var.set_frequencies(config[CONF_FREQ_MHZ], config[CONF_DEVIATION_KHZ]))
    cg.add(var.set_speed_count(config[CONF_SPEED_COUNT]))
