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

DEPENDENCIES = ["spi"]

QuietCoolFan = quiet_cool_ns.class_("QuietCoolFan", cg.Component, fan.Fan, spi.SPIDevice)

CONFIG_SCHEMA = fan.FAN_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(QuietCoolFan),
        cv.Required(CONF_GDO0_PIN): cv.uint8_t,
        cv.Required(CONF_GDO2_PIN): cv.uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=True))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])  # type: QuietCoolFan
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    await spi.register_spi_device(var, config)

    cs_num = config[spi.CONF_CS_PIN]["number"]
    cg.add(var.set_pins(cs_num, config[CONF_GDO0_PIN], config[CONF_GDO2_PIN]))
