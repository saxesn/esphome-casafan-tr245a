import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan

from . import TR245A, tr245a_ns

DEPENDENCIES = ["tr245a"]

TR245AFan = tr245a_ns.class_(
    "TR245AFan", fan.Fan, cg.Component, cg.Parented.template(TR245A)
)

CONF_TR245A_ID = "tr245a_id"

# TR245A has exactly six speed levels; TR245AFan::get_traits() advertises them.
CONFIG_SCHEMA = fan.fan_schema(TR245AFan, default_restore_mode="RESTORE_DEFAULT_OFF").extend(
    {
        cv.GenerateID(CONF_TR245A_ID): cv.use_id(TR245A),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await fan.new_fan(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_TR245A_ID])
