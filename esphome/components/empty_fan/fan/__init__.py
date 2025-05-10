import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, output, spi
from esphome.const import (
    CONF_DIRECTION_OUTPUT,
    CONF_OSCILLATION_OUTPUT,
    CONF_OUTPUT,
    CONF_OUTPUT_ID,
)
from .. import empty_fan_ns

DEPENDENCIES = ["spi"]

EmptyFan = empty_fan_ns.class_("EmptyFan", cg.Component, fan.Fan, spi.SPIDevice)

CONFIG_SCHEMA = fan.FAN_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(EmptyFan),
        cv.Required(CONF_OUTPUT): cv.use_id(output.BinaryOutput),
        cv.Optional(CONF_DIRECTION_OUTPUT): cv.use_id(output.BinaryOutput),
        cv.Optional(CONF_OSCILLATION_OUTPUT): cv.use_id(output.BinaryOutput),
    }
).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=True))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    await spi.register_spi_device(var, config)

    output_ = await cg.get_variable(config[CONF_OUTPUT])
    cg.add(var.set_output(output_))

    if oscillation_output_id := config.get(CONF_OSCILLATION_OUTPUT):
        oscillation_output = await cg.get_variable(oscillation_output_id)
        cg.add(var.set_oscillating(oscillation_output))

    if direction_output_id := config.get(CONF_DIRECTION_OUTPUT):
        direction_output = await cg.get_variable(direction_output_id)
        cg.add(var.set_direction(direction_output))
