import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import esp32, spi
from esphome.components.esp32.const import VARIANT_ESP32
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.core import TimePeriod

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["fan", "button"]

tr245a_ns = cg.esphome_ns.namespace("tr245a")
TR245A = tr245a_ns.class_("TR245A", cg.Component, spi.SPIDevice)
TR245AReceiveTrigger = tr245a_ns.class_(
    "TR245AReceiveTrigger", automation.Trigger.template(cg.uint16, cg.uint8, cg.uint8)
)

CONF_TXEN_PIN = "txen_pin"  # Optional PA control; XL7105-D03 connects this to A7700 RXSW
CONF_RXEN_PIN = "rxen_pin"  # Optional LNA control; XL7105-D03 connects this to A7700 TXSW

def validate_front_end(config):
    if config.get("front_end", "GENERIC") == "XL7105_D03":
        if CONF_TXEN_PIN not in config or CONF_RXEN_PIN not in config:
            raise cv.Invalid("front_end: XL7105_D03 requires both txen_pin and rxen_pin")
    return config


def validate_reinitialize_interval(value):
    interval = cv.positive_time_period_milliseconds(value)
    if interval.total_milliseconds == 0:
        return interval  # Disable maintenance only; error recovery remains active.
    return cv.Range(min=TimePeriod(minutes=1), max=TimePeriod(hours=24))(interval)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TR245A),
            cv.Optional("front_end", default="GENERIC"): cv.enum({"GENERIC": 0, "XL7105_D03": 1}, upper=True),
            cv.Optional("address", default=0): cv.hex_uint16_t,
            cv.Optional("frequency_offset_hz", default=0): cv.int_range(min=-500000, max=500000),
            cv.Optional("receive", default=True): cv.boolean,
            cv.Optional("receive_deduplication", default="120ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=TimePeriod(milliseconds=20), max=TimePeriod(milliseconds=1000)),
            ),
            cv.Optional("reinitialize_interval", default="30min"): validate_reinitialize_interval,
            cv.Optional("transmit_repetitions", default=1): cv.int_range(min=1, max=5),
            cv.Optional("on_receive"): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TR245AReceiveTrigger)}
            ),
            # Optional external PA/LNA (XL7105-D03). Omit for a bare MD7105-SY (no PA).
            cv.Optional(CONF_TXEN_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_RXEN_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True)),
    validate_front_end,
)

FINAL_VALIDATE_SCHEMA = cv.All(
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
    esp32.only_on_variant(
        supported=[VARIANT_ESP32], msg_prefix="The tr245a component"
    ),
    spi.final_validate_device_schema("tr245a", require_mosi=True, require_miso=True),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    cg.add(var.set_front_end(config["front_end"]))
    cg.add(var.set_address(config["address"]))
    cg.add(var.set_frequency_offset(config["frequency_offset_hz"]))
    cg.add(var.set_receive(config["receive"]))
    cg.add(var.set_receive_deduplication(config["receive_deduplication"].total_milliseconds))
    cg.add(var.set_reinitialize_interval(config["reinitialize_interval"].total_milliseconds))
    cg.add(var.set_transmit_repetitions(config["transmit_repetitions"]))
    for conf in config.get("on_receive", []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.uint16, "address"), (cg.uint8, "command"), (cg.uint8, "value")], conf
        )
    if CONF_TXEN_PIN in config:
        txen = await cg.gpio_pin_expression(config[CONF_TXEN_PIN])
        cg.add(var.set_txen_pin(txen))
    if CONF_RXEN_PIN in config:
        rxen = await cg.gpio_pin_expression(config[CONF_RXEN_PIN])
        cg.add(var.set_rxen_pin(rxen))
