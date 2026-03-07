"""
rx8xxx time platform — RX8111CE / RX8130CE RTC driver for ESPHome.

This file is intentionally self-contained (no relative imports from __init__.py)
to avoid a circular import with esphome.components.time:
    time  →  rx8xxx/time.py  →  __init__.py  →  time  (partially initialised)
"""
import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, i2c, time
from esphome.const import (
    CONF_ID,
    CONF_MODEL,
    CONF_TRIGGER_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
)

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["binary_sensor"]

# ---------------------------------------------------------------------------
# Namespace / class references
# ---------------------------------------------------------------------------
rx8xxx_ns = cg.esphome_ns.namespace("rx8xxx")

RX8XXXComponent = rx8xxx_ns.class_("RX8XXXComponent", time.RealTimeClock, cg.Component)
RX8111Component = rx8xxx_ns.class_("RX8111Component", RX8XXXComponent)
RX8130Component = rx8xxx_ns.class_("RX8130Component", RX8XXXComponent)

WriteAction          = rx8xxx_ns.class_("WriteAction",          automation.Action)
ReadAction           = rx8xxx_ns.class_("ReadAction",           automation.Action)
ClearAlarmFlagAction = rx8xxx_ns.class_("ClearAlarmFlagAction", automation.Action)
ClearTimerFlagAction = rx8xxx_ns.class_("ClearTimerFlagAction", automation.Action)
SetAlarmAction      = rx8xxx_ns.class_("SetAlarmAction", automation.Action)
ScheduleAlarmInAction = rx8xxx_ns.class_("ScheduleAlarmInAction", automation.Action)

AlarmTrigger = rx8xxx_ns.class_("AlarmTrigger", automation.Trigger.template())
TimerTrigger = rx8xxx_ns.class_("TimerTrigger", automation.Trigger.template())

FoutFrequency = rx8xxx_ns.enum("FoutFrequency")
TimerClock    = rx8xxx_ns.enum("TimerClock")

# ---------------------------------------------------------------------------
# Configuration key constants
# ---------------------------------------------------------------------------
CONF_BATTERY_BACKUP   = "battery_backup"
CONF_BATTERY_CHARGING = "battery_charging"
CONF_FOUT_FREQUENCY   = "fout_frequency"

CONF_ALARM_SECOND  = "alarm_second"
CONF_ALARM_MINUTE  = "alarm_minute"
CONF_ALARM_HOUR    = "alarm_hour"
CONF_ALARM_WEEKDAY = "alarm_weekday"
CONF_ALARM_DAY     = "alarm_day"
CONF_ALARM_ENABLED = "alarm_enabled"
CONF_ON_ALARM      = "on_alarm"

CONF_TIMER_COUNT = "timer_count"
CONF_TIMER_CLOCK = "timer_clock"
CONF_TIMER_ENABLED = "timer_enabled"
CONF_ON_TIMER    = "on_timer"

CONF_VLF        = "vlf"
CONF_ALARM_FLAG = "alarm_flag"
CONF_TIMER_FLAG = "timer_flag"

CONF_XST         = "xst"
CONF_BATTERY_LOW = "battery_low"  # RX8111CE: VLOW; RX8130CE: VBFF (same user-facing name)
CONF_EVIN        = "evin"

CONF_VBLF = "vblf"

CONF_TIMESTAMP      = "timestamp_enabled"
CONF_DIGITAL_OFFSET = "digital_offset"
CONF_SECOND = "second"
CONF_MINUTE = "minute"
CONF_HOUR = "hour"
CONF_WEEKDAY = "weekday"
CONF_DAY = "day"
CONF_ENABLED = "enabled"
CONF_SECONDS = "seconds"
CONF_MINUTES = "minutes"
CONF_HOURS = "hours"
CONF_WEEKDAYS = "weekdays"
CONF_DAYS = "days"

# ---------------------------------------------------------------------------
# Enumeration maps
#
# MODEL_MAP: cv.enum(MODEL_MAP, upper=True) validates the user's model string
# and returns the VALUE — stored as a plain string (e.g. "RX8111CE") so that
# cross-field comparisons in _validate_config use string equality (== / !=)
# rather than MockObj identity (is / is not).
#
# Using MockObj identity for these comparisons is fragile: ESPHome may exec
# this module more than once across validation passes, producing new MockObj
# instances each time.  config[CONF_MODEL] (set during pass 1) would then be
# a different object from RX8111Component as seen in _validate_config (pass 2),
# causing false "not available on this model" errors even when the correct
# model is declared.
#
# MODEL_CLASS_MAP maps the validated model string → concrete C++ class for use
# in to_code(), where all references come from a single consistent module load.
# ---------------------------------------------------------------------------
MODEL_MAP = {
    "RX8111CE": "RX8111CE",
    "RX8130CE": "RX8130CE",
}

MODEL_CLASS_MAP = {
    "RX8111CE": RX8111Component,
    "RX8130CE": RX8130Component,
}

FOUT_FREQUENCY_MAP = {
    "32768hz": FoutFrequency.FOUT_32768_HZ,
    "1024hz":  FoutFrequency.FOUT_1024_HZ,
    "1hz":     FoutFrequency.FOUT_1_HZ,
    "off":     FoutFrequency.FOUT_OFF,
}

TIMER_CLOCK_MAP = {
    "4096hz":   TimerClock.TIMER_4096_HZ,
    "64hz":     TimerClock.TIMER_64_HZ,
    "1hz":      TimerClock.TIMER_1_HZ,
    "1_60hz":   TimerClock.TIMER_1_60_HZ,
    "1_3600hz": TimerClock.TIMER_1_3600_HZ,  # RX8130CE only; validated below
}


def _model_name(model):
    """Return the human-readable model string (config[CONF_MODEL] is already the name)."""
    return model


# ---------------------------------------------------------------------------
# Alarm field validators
# ---------------------------------------------------------------------------
def _make_alarm_validator(name, min_val, max_val, description):
    """Factory for alarm field range validators."""
    def validator(value):
        value = cv.int_(value)
        if not min_val <= value <= max_val:
            raise cv.Invalid(
                f"{name} must be {min_val}\u2013{max_val} ({description}), got {value}"
            )
        return value
    return validator


_validate_alarm_second = _make_alarm_validator(
    "alarm_second", 0, 59, "seconds within the minute"
)
_validate_alarm_minute = _make_alarm_validator(
    "alarm_minute", 0, 59, "minutes within the hour"
)
_validate_alarm_hour = _make_alarm_validator(
    "alarm_hour", 0, 23, "24-hour clock; midnight=0, noon=12"
)
_validate_alarm_weekday = _make_alarm_validator(
    "alarm_weekday", 1, 7,
    "1=Sunday, 2=Monday, 3=Tuesday, 4=Wednesday, "
    "5=Thursday, 6=Friday, 7=Saturday"
)
_validate_alarm_day = _make_alarm_validator(
    "alarm_day", 1, 31, "day of month"
)


# ---------------------------------------------------------------------------
# Cross-field validation
# ---------------------------------------------------------------------------
def _validate_config(config):
    # config[CONF_MODEL] is the plain string returned by cv.enum:
    #   "RX8111CE"  or  "RX8130CE"
    model = config[CONF_MODEL]

    # ---- Battery -----------------------------------------------------------
    if config.get(CONF_BATTERY_CHARGING, False) and not config.get(CONF_BATTERY_BACKUP, False):
        raise cv.Invalid("'battery_charging' requires 'battery_backup: true'")

    # ---- Timer clock -------------------------------------------------------
    if (
        config.get(CONF_TIMER_CLOCK) is TIMER_CLOCK_MAP["1_3600hz"]
        and model != "RX8130CE"
    ):
        raise cv.Invalid(
            "Timer clock '1_3600hz' (once per hour) is only available on the RX8130CE"
        )

    # ---- timer_count: model-dependent upper limit --------------------------
    if CONF_TIMER_COUNT in config:
        if model == "RX8111CE":
            max_count, bits = 16_777_215, "24-bit"
        else:
            max_count, bits = 65_535, "16-bit"
        if config[CONF_TIMER_COUNT] > max_count:
            raise cv.Invalid(
                f"'timer_count' value {config[CONF_TIMER_COUNT]:,} exceeds the "
                f"{_model_name(model)} {bits} timer counter maximum of {max_count:,}. "
                f"Reduce timer_count or use a slower timer_clock to achieve longer intervals."
            )

    # ---- Explicit enable override validation -------------------------------
    if config.get(CONF_TIMER_ENABLED, False) and CONF_TIMER_COUNT not in config:
        raise cv.Invalid(
            "'timer_enabled: true' requires 'timer_count' so the timer has a defined period"
        )

    # ---- Model-specific feature availability --------------------------------
    _MODEL_FEATURES = [
        (CONF_ALARM_SECOND, "RX8111CE",
         "'alarm_second' (per-second alarm matching) is only available on the "
         "RX8111CE. The RX8130CE does not have a seconds alarm register."),
        (CONF_TIMESTAMP, "RX8111CE",
         "'timestamp_enabled' is only available on the RX8111CE"),
        (CONF_XST, "RX8111CE",
         "'xst' binary sensor is only available on the RX8111CE"),
        (CONF_EVIN, "RX8111CE",
         "'evin' binary sensor is only available on the RX8111CE"),
        (CONF_DIGITAL_OFFSET, "RX8130CE",
         "'digital_offset' is only available on the RX8130CE"),
        (CONF_VBLF, "RX8130CE",
         "'vblf' binary sensor is only available on the RX8130CE"),
    ]
    for conf_key, required_model, msg in _MODEL_FEATURES:
        val = config.get(conf_key)
        if val is not None and val is not False and model != required_model:
            raise cv.Invalid(msg)

    # ---- Alarm field mutual exclusion --------------------------------------
    if CONF_ALARM_WEEKDAY in config and CONF_ALARM_DAY in config:
        raise cv.Invalid(
            "Cannot specify both 'alarm_weekday' and 'alarm_day'. "
            "The RTC uses the WADA bit to select either weekday matching OR "
            "day-of-month matching — not both simultaneously. "
            "Use 'alarm_weekday' (1=Sun … 7=Sat) to match a day of the week, "
            "or 'alarm_day' (1–31) to match a calendar date. Remove one field."
        )

    # ---- alarm_day: warn for days that do not exist in all months ----------
    if CONF_ALARM_DAY in config:
        day = config[CONF_ALARM_DAY]
        if day == 29:
            _LOGGER.warning(
                "rx8xxx: alarm_day=29 — February has only 28 days in non-leap years. "
                "The RTC alarm will silently skip February in non-leap years."
            )
        elif day == 30:
            _LOGGER.warning(
                "rx8xxx: alarm_day=30 — February never has 30 days. "
                "The RTC alarm will silently skip February every year."
            )
        elif day == 31:
            _LOGGER.warning(
                "rx8xxx: alarm_day=31 — only January, March, May, July, August, October, "
                "and December have 31 days. The RTC alarm will silently skip all other months "
                "(April, June, September, November, and February). "
                "The maximum gap between alarms can reach ~59 days "
                "(e.g., February 1 → March 31)."
            )

    return config


# ---------------------------------------------------------------------------
# CONFIG_SCHEMA
# ---------------------------------------------------------------------------
CONFIG_SCHEMA = cv.All(
    time.TIME_SCHEMA.extend(
        {
            # cv.enum(MODEL_MAP, upper=True) returns the MAP VALUE (the class
            # object — RX8111Component or RX8130Component), not the key string.
            # The declared ID type starts as RX8XXXComponent; to_code overrides
            # it to the concrete subclass before calling cg.new_Pvariable so
            # the generated C++ instantiates the correct subclass.
            cv.GenerateID(): cv.declare_id(RX8XXXComponent),
            cv.Required(CONF_MODEL): cv.enum(MODEL_MAP, upper=True),

            # ---- Battery backup -------------------------------------------
            cv.Optional(CONF_BATTERY_BACKUP, default=False): cv.boolean,
            cv.Optional(CONF_BATTERY_CHARGING, default=False): cv.boolean,

            # ---- FOUT clock output ----------------------------------------
            cv.Optional(CONF_FOUT_FREQUENCY, default="off"): cv.enum(
                FOUT_FREQUENCY_MAP, lower=True
            ),

            # ---- Alarm configuration --------------------------------------
            # Omit a field to leave that alarm register disabled (AE=1 in hardware).
            # alarm_weekday and alarm_day are mutually exclusive (validated below).
            cv.Optional(CONF_ALARM_SECOND):  _validate_alarm_second,   # RX8111CE only; 0–59
            cv.Optional(CONF_ALARM_MINUTE):  _validate_alarm_minute,   # 0–59
            cv.Optional(CONF_ALARM_HOUR):    _validate_alarm_hour,     # 0–23 (24-hour clock)
            cv.Optional(CONF_ALARM_WEEKDAY): _validate_alarm_weekday,  # 1=Sun, 2=Mon … 7=Sat
            cv.Optional(CONF_ALARM_DAY):     _validate_alarm_day,      # 1–31; warn for 29–31
            cv.Optional(CONF_ALARM_ENABLED): cv.boolean,
            cv.Optional(CONF_ON_ALARM): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(AlarmTrigger)}
            ),

            # ---- Wake-up timer --------------------------------------------
            # Max count is model-dependent: 16,777,215 (24-bit, RX8111CE) or
            # 65,535 (16-bit, RX8130CE). The schema enforces the 24-bit ceiling;
            # the 16-bit ceiling for RX8130CE is enforced in _validate_config.
            cv.Optional(CONF_TIMER_COUNT): cv.int_range(min=1, max=16_777_215),
            cv.Optional(CONF_TIMER_CLOCK, default="1hz"): cv.enum(
                TIMER_CLOCK_MAP, lower=True
            ),
            cv.Optional(CONF_TIMER_ENABLED): cv.boolean,
            cv.Optional(CONF_ON_TIMER): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TimerTrigger)}
            ),

            # ---- Binary sensor sub-components (common) --------------------
            cv.Optional(CONF_VLF): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_PROBLEM,
            ),
            cv.Optional(CONF_ALARM_FLAG): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_TIMER_FLAG): binary_sensor.binary_sensor_schema(),

            # ---- Binary sensor sub-components (RX8111CE only) -------------
            cv.Optional(CONF_XST): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_PROBLEM,
            ),
            cv.Optional(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY,
            ),
            cv.Optional(CONF_EVIN): binary_sensor.binary_sensor_schema(),

            # ---- Binary sensor sub-components (RX8130CE only) -------------
            cv.Optional(CONF_VBLF): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY,
            ),
            # battery_low also used for RX8130CE VBFF (same CONF key, different register)

            # ---- RX8111CE-specific ----------------------------------------
            cv.Optional(CONF_TIMESTAMP, default=False): cv.boolean,

            # ---- RX8130CE-specific ----------------------------------------
            cv.Optional(CONF_DIGITAL_OFFSET): cv.int_range(min=-64, max=63),
        }
    ).extend(i2c.i2c_device_schema(0x32)),
    _validate_config,
)


# ---------------------------------------------------------------------------
# to_code — generate C++ initialization calls
# ---------------------------------------------------------------------------
async def to_code(config):
    # config[CONF_MODEL] is the validated model string ("RX8111CE" or "RX8130CE").
    # Look up the concrete C++ class from MODEL_CLASS_MAP — all references here
    # are from the same module load, so 'is' identity comparisons below are safe.
    model_class = MODEL_CLASS_MAP[config[CONF_MODEL]]

    # Override the declared ID type to the concrete subclass just long enough
    # for cg.new_Pvariable to emit:
    #   new rx8xxx::RX8111Component()   or   new rx8xxx::RX8130Component()
    # rather than the invalid `new rx8xxx::RX8XXXComponent()` (abstract).
    #
    # IMPORTANT: reset the type back to the abstract base (RX8XXXComponent)
    # BEFORE calling register_component.  ESPHome 2026.x keys CORE.variables by
    # (ID name, ID type).  All automation action schemas (_PARENT_SCHEMA) use
    # cv.use_id(RX8XXXComponent), so the registered entry must carry the abstract
    # type or get_variable will wait forever → "circular dependency" error.
    config[CONF_ID].type = model_class          # concrete type for `new` expression
    var = cg.new_Pvariable(config[CONF_ID])     # captures type string eagerly
    config[CONF_ID].type = RX8XXXComponent      # reset to abstract for registry

    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    await time.register_time(var, config)

    # Battery backup
    cg.add(var.set_battery_backup(config[CONF_BATTERY_BACKUP]))
    cg.add(var.set_battery_charging(config[CONF_BATTERY_CHARGING]))

    # FOUT frequency
    cg.add(var.set_fout_frequency(config[CONF_FOUT_FREQUENCY]))

    # ---- Alarm configuration ----------------------------------------------
    has_alarm_fields = any(
        k in config
        for k in (
            CONF_ALARM_SECOND,
            CONF_ALARM_MINUTE,
            CONF_ALARM_HOUR,
            CONF_ALARM_WEEKDAY,
            CONF_ALARM_DAY,
        )
    )
    alarm_enabled = has_alarm_fields or CONF_ON_ALARM in config
    if CONF_ALARM_ENABLED in config:
        alarm_enabled = config[CONF_ALARM_ENABLED]

    cg.add(var.set_alarm_second(config.get(CONF_ALARM_SECOND, 0xFF)))
    cg.add(var.set_alarm_minute(config.get(CONF_ALARM_MINUTE, 0xFF)))
    cg.add(var.set_alarm_hour(config.get(CONF_ALARM_HOUR, 0xFF)))
    cg.add(var.set_alarm_weekday(config.get(CONF_ALARM_WEEKDAY, 0xFF)))
    cg.add(var.set_alarm_day(config.get(CONF_ALARM_DAY, 0xFF)))

    # ---- Common binary sensors --------------------------------------------
    # Build referenced entities before automation blocks so callback lambdas
    # can resolve id(...) lookups without creating async dependency cycles.
    if CONF_VLF in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_VLF])
        cg.add(var.set_vlf_binary_sensor(sens))

    if CONF_ALARM_FLAG in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_ALARM_FLAG])
        cg.add(var.set_alarm_flag_binary_sensor(sens))

    if CONF_TIMER_FLAG in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_TIMER_FLAG])
        cg.add(var.set_timer_flag_binary_sensor(sens))

    # ---- RX8111CE binary sensors ------------------------------------------
    if CONF_XST in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_XST])
        cg.add(var.set_xst_binary_sensor(sens))

    if CONF_BATTERY_LOW in config and model_class is RX8111Component:
        sens = await binary_sensor.new_binary_sensor(config[CONF_BATTERY_LOW])
        cg.add(var.set_battery_low_binary_sensor(sens))

    if CONF_EVIN in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_EVIN])
        cg.add(var.set_evin_binary_sensor(sens))

    # ---- RX8130CE binary sensors ------------------------------------------
    if CONF_BATTERY_LOW in config and model_class is RX8130Component:
        sens = await binary_sensor.new_binary_sensor(config[CONF_BATTERY_LOW])
        cg.add(var.set_battery_low_binary_sensor(sens))

    if CONF_VBLF in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_VBLF])
        cg.add(var.set_vblf_binary_sensor(sens))

    # ---- RX8111CE-specific features ---------------------------------------
    if model_class is RX8111Component:
        cg.add(var.set_timestamp_enabled(config.get(CONF_TIMESTAMP, False)))

    # ---- RX8130CE-specific features ---------------------------------------
    if model_class is RX8130Component and CONF_DIGITAL_OFFSET in config:
        cg.add(var.set_digital_offset(config[CONF_DIGITAL_OFFSET]))

    if CONF_ON_ALARM in config:
        for conf in config[CONF_ON_ALARM]:
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
            await automation.build_automation(trigger, [], conf)
            cg.add(var.add_on_alarm_callback(trigger))

    cg.add(var.set_alarm_enabled(alarm_enabled))

    # ---- Wake-up timer ----------------------------------------------------
    timer_enabled = CONF_TIMER_COUNT in config or CONF_ON_TIMER in config
    if CONF_TIMER_ENABLED in config:
        timer_enabled = config[CONF_TIMER_ENABLED]

    if CONF_TIMER_COUNT in config:
        cg.add(var.set_timer_count(config[CONF_TIMER_COUNT]))
        cg.add(var.set_timer_clock(config[CONF_TIMER_CLOCK]))

    if CONF_ON_TIMER in config:
        for conf in config[CONF_ON_TIMER]:
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
            await automation.build_automation(trigger, [], conf)
            cg.add(var.add_on_timer_callback(trigger))

    cg.add(var.set_timer_enabled(timer_enabled))


# ---------------------------------------------------------------------------
# Automation action schemas
# ---------------------------------------------------------------------------
_PARENT_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(RX8XXXComponent)})


def _validate_set_alarm_action(config):
    has_time_fields = any(
        key in config for key in (CONF_SECOND, CONF_MINUTE, CONF_HOUR, CONF_WEEKDAY, CONF_DAY)
    )
    if CONF_WEEKDAY in config and CONF_DAY in config:
        raise cv.Invalid("rx8xxx.set_alarm cannot include both 'weekday' and 'day'")
    if not has_time_fields and CONF_ENABLED not in config:
        raise cv.Invalid(
            "rx8xxx.set_alarm requires at least one time field "
            "(second/minute/hour/weekday/day) or 'enabled'"
        )
    return config


def _normalize_set_alarm_action_keys(config):
    out = dict(config)
    alias_map = {
        CONF_SECONDS: CONF_SECOND,
        CONF_MINUTES: CONF_MINUTE,
        CONF_HOURS: CONF_HOUR,
        CONF_WEEKDAYS: CONF_WEEKDAY,
        CONF_DAYS: CONF_DAY,
    }
    for alias, canonical in alias_map.items():
        if alias in out and canonical in out:
            raise cv.Invalid(
                f"rx8xxx.set_alarm cannot include both '{canonical}' and '{alias}'"
            )
        if alias in out:
            out[canonical] = out.pop(alias)
    return out


def _normalize_schedule_alarm_in_keys(config):
    out = dict(config)
    alias_map = {
        CONF_SECOND: CONF_SECONDS,
        CONF_MINUTE: CONF_MINUTES,
        CONF_HOUR: CONF_HOURS,
        CONF_DAY: CONF_DAYS,
    }
    for singular, plural in alias_map.items():
        if singular in out and plural in out:
            raise cv.Invalid(
                f"rx8xxx.schedule_alarm_in cannot include both '{singular}' and '{plural}'"
            )
        if singular in out:
            out[plural] = out.pop(singular)

    seconds = out.get(CONF_SECONDS, 0)
    minutes = out.get(CONF_MINUTES, 0)
    hours = out.get(CONF_HOURS, 0)
    days = out.get(CONF_DAYS, 0)
    total_seconds = seconds + minutes * 60 + hours * 3600 + days * 86400

    if total_seconds <= 0:
        raise cv.Invalid(
            "rx8xxx.schedule_alarm_in requires at least one duration field: "
            "'seconds/minutes/hours/days' (singular aliases also accepted)"
        )
    if total_seconds > 2419200:
        raise cv.Invalid(
            "rx8xxx.schedule_alarm_in total duration must be <= 2,419,200 seconds (28 days). "
            "The alarm hardware has no month register, so durations beyond the shortest "
            "month (28 days) risk the day-of-month matching in the wrong month."
        )

    out[CONF_SECONDS] = total_seconds
    return out


@automation.register_action("rx8xxx.write_time", WriteAction, _PARENT_SCHEMA)
async def rx8xxx_write_time_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("rx8xxx.read_time", ReadAction, _PARENT_SCHEMA)
async def rx8xxx_read_time_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("rx8xxx.clear_alarm_flag", ClearAlarmFlagAction, _PARENT_SCHEMA)
async def rx8xxx_clear_alarm_flag_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action("rx8xxx.clear_timer_flag", ClearTimerFlagAction, _PARENT_SCHEMA)
async def rx8xxx_clear_timer_flag_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "rx8xxx.set_alarm",
    SetAlarmAction,
    cv.All(
        _PARENT_SCHEMA.extend(
            {
                cv.Optional(CONF_ENABLED): cv.boolean,
                cv.Optional(CONF_SECOND): _validate_alarm_second,
                cv.Optional(CONF_SECONDS): _validate_alarm_second,
                cv.Optional(CONF_MINUTE): _validate_alarm_minute,
                cv.Optional(CONF_MINUTES): _validate_alarm_minute,
                cv.Optional(CONF_HOUR): _validate_alarm_hour,
                cv.Optional(CONF_HOURS): _validate_alarm_hour,
                cv.Optional(CONF_WEEKDAY): _validate_alarm_weekday,
                cv.Optional(CONF_WEEKDAYS): _validate_alarm_weekday,
                cv.Optional(CONF_DAY): _validate_alarm_day,
                cv.Optional(CONF_DAYS): _validate_alarm_day,
            }
        ),
        _normalize_set_alarm_action_keys,
        _validate_set_alarm_action,
    ),
)
async def rx8xxx_set_alarm_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_ENABLED in config:
        cg.add(var.set_enabled(config[CONF_ENABLED]))
    if CONF_SECOND in config:
        cg.add(var.set_second(config[CONF_SECOND]))
    if CONF_MINUTE in config:
        cg.add(var.set_minute(config[CONF_MINUTE]))
    if CONF_HOUR in config:
        cg.add(var.set_hour(config[CONF_HOUR]))
    if CONF_WEEKDAY in config:
        cg.add(var.set_weekday(config[CONF_WEEKDAY]))
    if CONF_DAY in config:
        cg.add(var.set_day(config[CONF_DAY]))
    return var


@automation.register_action(
    "rx8xxx.schedule_alarm_in",
    ScheduleAlarmInAction,
    cv.All(
        _PARENT_SCHEMA.extend(
            {
                cv.Optional(CONF_SECONDS): cv.int_range(min=0, max=2419200),
                cv.Optional(CONF_SECOND): cv.int_range(min=0, max=2419200),
                cv.Optional(CONF_MINUTES): cv.int_range(min=0, max=40320),
                cv.Optional(CONF_MINUTE): cv.int_range(min=0, max=40320),
                cv.Optional(CONF_HOURS): cv.int_range(min=0, max=672),
                cv.Optional(CONF_HOUR): cv.int_range(min=0, max=672),
                cv.Optional(CONF_DAYS): cv.int_range(min=0, max=28),
                cv.Optional(CONF_DAY): cv.int_range(min=0, max=28),
            }
        ),
        _normalize_schedule_alarm_in_keys,
    ),
)
async def rx8xxx_schedule_alarm_in_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_seconds(config[CONF_SECONDS]))
    return var
