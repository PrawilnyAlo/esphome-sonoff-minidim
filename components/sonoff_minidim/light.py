import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_OUTPUT_ID

from . import CONF_SONOFF_MINIDIM_ID, SonoffMiniDim, sonoff_minidim_ns

DEPENDENCIES = ["sonoff_minidim"]

SonoffMiniDimLight = sonoff_minidim_ns.class_("SonoffMiniDimLight", light.LightOutput)

CONFIG_SCHEMA = light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(SonoffMiniDimLight),
        cv.GenerateID(CONF_SONOFF_MINIDIM_ID): cv.use_id(SonoffMiniDim),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    parent = await cg.get_variable(config[CONF_SONOFF_MINIDIM_ID])
    cg.add(var.set_parent(parent))
