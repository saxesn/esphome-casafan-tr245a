import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from . import TR245A, tr245a_ns

DEPENDENCIES = ["tr245a"]

TR245AButton = tr245a_ns.class_("TR245AButton", button.Button, cg.Parented.template(TR245A))

CONF_TR245A_ID = "tr245a_id"
CONF_COMMAND = "command"
COMMANDS = {"UP": 0, "DOWN": 1, "OFF": 2, "FAN_R": 3, "SET": 4, "PAIR": 4}

CONFIG_SCHEMA = button.button_schema(TR245AButton).extend(
    {
        cv.GenerateID(CONF_TR245A_ID): cv.use_id(TR245A),
        cv.Required(CONF_COMMAND): cv.enum(COMMANDS, upper=True),
    }
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_parented(var, config[CONF_TR245A_ID])
    cg.add(var.set_command(config[CONF_COMMAND]))
