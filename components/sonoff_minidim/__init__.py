import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import sensor, uart
from esphome.const import (
    CONF_CURRENT,
    CONF_ID,
    CONF_POWER,
    CONF_RESET_PIN,
    CONF_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

CODEOWNERS = []
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

CONF_SONOFF_MINIDIM_ID = "sonoff_minidim_id"
CONF_RESET_ON_BOOT = "reset_on_boot"
CONF_LEVEL = "level"
CONF_CALIBRATION_MIN = "calibration_min"
CONF_CALIBRATION_MAX = "calibration_max"
CONF_CALIBRATION_PROGRESS = "calibration_progress"

sonoff_minidim_ns = cg.esphome_ns.namespace("sonoff_minidim")
SonoffMiniDim = sonoff_minidim_ns.class_(
    "SonoffMiniDim", cg.PollingComponent, uart.UARTDevice
)

_DIAG_RAW = sensor.sensor_schema(
    accuracy_decimals=0,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SonoffMiniDim),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_RESET_ON_BOOT, default=False): cv.boolean,
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_LEVEL): _DIAG_RAW,
            cv.Optional(CONF_CALIBRATION_MIN): _DIAG_RAW,
            cv.Optional(CONF_CALIBRATION_MAX): _DIAG_RAW,
            cv.Optional(CONF_CALIBRATION_PROGRESS): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "sonoff_minidim", baud_rate=115200, require_tx=True, require_rx=True
)

_SENSORS = {
    CONF_POWER: "set_power_sensor",
    CONF_VOLTAGE: "set_voltage_sensor",
    CONF_CURRENT: "set_current_sensor",
    CONF_LEVEL: "set_level_sensor",
    CONF_CALIBRATION_MIN: "set_calibration_min_sensor",
    CONF_CALIBRATION_MAX: "set_calibration_max_sensor",
    CONF_CALIBRATION_PROGRESS: "set_calibration_progress_sensor",
}


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_RESET_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(pin))
        cg.add(var.set_reset_on_boot(config[CONF_RESET_ON_BOOT]))

    for key, setter in _SENSORS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))
