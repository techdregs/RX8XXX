
Note: This should be considered as a beta. I have tested heavily in ESPHome to verify features work correctly, but there are probably edge cases that I haven't considered. Feel free to submit bugs. I have not tested the regular embedded libraries as much, so I anticipate those might have more issues.

Also, I do not plan on adding features, so if you need modifications beyond bugs, you probably want to fork this.


# ESPHome RX8XXX RTC Component

ESPHome external component for **Epson RX8111CE** and **RX8130CE** real-time clock ICs over I2C.

Provides hardware timekeeping with alarm, countdown timer, event detection, hardware timestamping, battery backup, clock output, and status monitoring — all configurable from YAML.

## Supported Models

| Model | Timer | Alarm Second | EVIN Event Detection | Timestamp Engine | Digital Offset | Timer 1/3600 Hz |
|-------|-------|-------------|---------------------|------------------|----------------|-----------------|
| RX8111CE | 24-bit (max 16,777,215) | Yes | Yes | Yes | No | No |
| RX8130CE | 16-bit (max 65,535) | No | No | No | Yes | Yes |

## Installation

Add this repository as an external component in your ESPHome YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/tech_dregs/RX8XXX
    components: [rx8xxx]
```

Or for local development (place the rx8xxx folder into a /ESPHome/components folder
in your Home Assistant installation):

```yaml
external_components:
  - source:
      type: local
      path: components # ie. files are located in /Esphome/components/rx8xxx/
    components: [rx8xxx]
```

## Quick Start

### RX8111CE Minimal Example

```yaml
i2c:
  sda: GPIO23 #your SDA pin
  scl: GPIO22 #your SCL pin

time:
  - platform: rx8xxx
    id: my_rtc
    model: RX8111CE

  - platform: homeassistant
    on_time_sync:
      - rx8xxx.write_time:
          id: my_rtc
```

### RX8130CE Minimal Example

```yaml
i2c:
  sda: GPIO23 #your SDA pin
  scl: GPIO22 #your SCL pin

time:
  - platform: rx8xxx
    id: my_rtc
    model: RX8130CE

  - platform: homeassistant
    on_time_sync:
      - rx8xxx.write_time:
          id: my_rtc
```

## Configuration Reference

### Core

```yaml
time:
  - platform: rx8xxx
    id: my_rtc
    model: RX8111CE          # Required. "RX8111CE" or "RX8130CE"
    update_interval: 2s      # How often to poll flags and read time
```

The I2C address is fixed at `0x32` (hardware default, not configurable).

---

### Battery Backup

```yaml
    # read the manual, this probably doesn't do what you think
    battery_backup: false    # Enable automatic backup power switchover (default: false)
    battery_charging: false  # Enable trickle charging for rechargeable cell (default: false)
```

| Option | Type | Default | Notes |
|--------|------|---------|-------|
| `battery_backup` | boolean | `false` | Enables the chip's internal power switchover to backup battery |
| `battery_charging` | boolean | `false` | Enables trickle charging. **Requires** `battery_backup: true` |

> **Warning:** Only enable `battery_charging` if you have a rechargeable backup cell. Charging a non-rechargeable cell (e.g., CR2032) can cause damage. Read the manual.

---

### FOUT Clock Output

```yaml
    fout_frequency: "off"    # "32768hz", "1024hz", "1hz", or "off" (default: "off")
```

Drives the chip's FOUT pin with a square wave at the selected frequency. Useful for external frequency measurement (e.g., with ESPHome's `pulse_counter` sensor) or providing a clock signal to other circuits.

---

### Alarm

The alarm triggers when the current time matches **all enabled alarm fields** simultaneously. Omit a field to ignore it (match any value for that field).

Example: setting only `alarm_hour: 5` will fire at the start of hour 5 (5:00:00). Note: if you clear AF while still in the matching window (during hour 5), the alarm re-fires immediately since the hour still matches.

```yaml
    alarm_second: 30         # 0-59, RX8111CE only
    alarm_minute: 0          # 0-59
    alarm_hour: 12           # 0-23 (24-hour format)
    alarm_weekday: 3         # 1=Sun, 2=Mon, 3=Tue, 4=Wed, 5=Thu, 6=Fri, 7=Sat
    alarm_day: 15            # 1-31, day of month
    alarm_enabled: true      # Explicit enable/disable override
```

| Option | Type | Range | Model | Notes |
|--------|------|-------|-------|-------|
| `alarm_second` | int | 0–59 | RX8111CE only | Per-second matching |
| `alarm_minute` | int | 0–59 | Both | |
| `alarm_hour` | int | 0–23 | Both | 24-hour clock |
| `alarm_weekday` | int | 1–7 | Both | 1=Sunday … 7=Saturday |
| `alarm_day` | int | 1–31 | Both | Day of month |
| `alarm_enabled` | boolean | — | Both | Override auto-detection of whether alarm is enabled |

**Rules:**
- `alarm_weekday` and `alarm_day` are **mutually exclusive** — you cannot specify both (hardware limitation: the WADA bit selects one mode).
- If any alarm field or `on_alarm` trigger is present, the alarm is automatically enabled. Use `alarm_enabled: false` to override this.
- Omit all fields except the ones you want to match. For example, setting only `alarm_second: 30` fires the alarm at second :30 of every minute of every hour of every day.

**Day-of-month warnings** (compile-time):
- `alarm_day: 29` — skips February in non-leap years
- `alarm_day: 30` — skips February every year
- `alarm_day: 31` — skips April, June, September, November, and February (up to ~59-day gap between alarms)

---

### Timer (Countdown)

The timer counts down from `timer_count` at the rate set by `timer_clock`, then fires and repeats. Timers do fire the /INT pin (bring it low), however they do not hold that state, and automatically reset the /INT pin after a few milliseconds. Consult the manual for details.

```yaml
    timer_count: 60          # 1–16,777,215 (RX8111CE) or 1–65,535 (RX8130CE)
    timer_clock: "1hz"       # "4096hz", "64hz", "1hz", "1_60hz", "1_3600hz"
    timer_enabled: true      # Explicit enable/disable override
```

| Option | Type | Default | Notes |
|--------|------|---------|-------|
| `timer_count` | int | — | Number of ticks before the timer fires |
| `timer_clock` | enum | `"1hz"` | Timer source clock frequency |
| `timer_enabled` | boolean | — | Override auto-detection |

**Timer clock options:**

| Value | Frequency | Example: timer_count=60 fires every… |
|-------|-----------|--------------------------------------|
| `"4096hz"` | 4096 Hz | ~14.6 ms |
| `"64hz"` | 64 Hz | ~937.5 ms |
| `"1hz"` | 1 Hz | 60 seconds |
| `"1_60hz"` | 1/60 Hz | 60 minutes |
| `"1_3600hz"` | 1/3600 Hz | 60 hours (**RX8130CE only**) |

**Rules:**
- `timer_enabled: true` requires `timer_count` to be set.
- `"1_3600hz"` is only available on RX8130CE. On RX8111CE it will produce a compile-time error.
- If `timer_count` or `on_timer` is present, the timer is auto-enabled. Use `timer_enabled: false` to override.

---

### Binary Sensors

Binary sensors are defined **inline** within the `rx8xxx` time platform block. All standard ESPHome binary sensor options (name, id, filters, etc.) are supported.

#### Both Models

```yaml
    vlf:
      name: "Voltage Low Flag"        # device_class: problem

    alarm_flag:
      name: "Alarm Flag"

    timer_flag:
      name: "Timer Flag"
```

| Sensor | Description | Device Class |
|--------|-------------|--------------|
| `vlf` | Voltage Low Flag — oscillation was lost (backup battery too low or absent) | `problem` |
| `alarm_flag` | Alarm Flag (AF) — alarm has fired and not been cleared | — |
| `timer_flag` | Timer Flag (TF) — timer has fired and not been cleared | — |

#### RX8111CE Only

```yaml
    xst:
      name: "Crystal Stop"            # device_class: problem

    battery_low:
      name: "Battery Low"             # device_class: battery

    evin:
      name: "Event Input State"

    event_flag:
      name: "Event Flag"
```

| Sensor | Description | Device Class |
|--------|-------------|--------------|
| `xst` | Crystal oscillation stop detected since last power-on | `problem` |
| `battery_low` | VBAT low detection (from status register) | `battery` |
| `evin` | Real-time EVIN pin voltage level | — |
| `event_flag` | Event Flag (EVF) — EVIN event detected and not been cleared | — |

#### RX8130CE Only

```yaml
    battery_low:
      name: "Backup Battery Failure"  # device_class: battery

    vblf:
      name: "VDD Level Low"           # device_class: battery
```

| Sensor | Description | Device Class |
|--------|-------------|--------------|
| `battery_low` | Backup battery failure flag (VBFF) | `battery` |
| `vblf` | VDD battery level low flag | `battery` |

> **Note on `battery_low`:** This sensor is available on **both models** using the same YAML key, but reads different hardware registers internally (RX8111CE: VLOW in status register 0x33; RX8130CE: VBFF in flag register 0x1D). The low-voltage detection threshold is fixed in hardware at approximately 1.2V and is **not configurable**. This is designed for coin cells (CR2032, ML2032, etc.) — it is **not suitable for monitoring LiPo or other higher-voltage batteries**.

---

### Model-Specific Options

#### RX8111CE

```yaml
    timestamp_enabled: false          # Keep hardware timestamp capture active (default: false)
    timestamp_record_mode: latest     # "latest", "stop_when_full", or "overwrite" (default: latest)
    event_level: low                  # EVIN detection level: "low" or "high"
    evin_filter: 0                    # Chattering filter: 0=off, 1=3.9ms, 2=15.6ms, 3=125ms
    evin_pull: none                   # Internal pull resistor: see table below
    event_timestamp:
      name: "RTC Event Timestamp"     # HA stores the published timestamp history
```

| Option | Type | Default | Notes |
|--------|------|---------|-------|
| `event_level` | enum | `low` | `"low"` or `"high"` — which EVIN voltage level triggers detection |
| `evin_filter` | int | `0` | Hardware chattering filter level (0–3) |
| `evin_pull` | enum | `none` | Internal pull-up/pull-down resistor on EVIN pin |
| `timestamp_enabled` | boolean | `false` | Keep hardware timestamp capture active |
| `timestamp_record_mode` | enum | `latest` | How the chip stores timestamp records |

**EVIN chattering filter (`evin_filter`):**

| Value | Filter Period | Description |
|-------|--------------|-------------|
| `0` | None | No filter (default) |
| `1` | 3.9 ms | Rejects noise shorter than 3.9 ms |
| `2` | 15.6 ms | Rejects noise shorter than 15.6 ms |
| `3` | 125 ms | Rejects noise shorter than 125 ms |

**EVIN pull resistor (`evin_pull`):**

| Value | Description |
|-------|-------------|
| `none` | Hi-Z — no internal connection (default) |
| `pullup_500k` | Pull-up 500 kOhm to VOUT |
| `pullup_1m` | Pull-up 1 MOhm to VOUT |
| `pullup_10m` | Pull-up 10 MOhm to VOUT |
| `pulldown_500k` | Pull-down 500 kOhm to GND |

> **Tip:** If the EVIN pin floats or produces spurious events, configure a pull resistor to match your idle state and a chattering filter to reject noise. For example, with `event_level: low` (trigger on low), use `evin_pull: pullup_500k` to keep the pin high at idle, and `evin_filter: 2` for 15.6 ms debouncing.

The RX8111CE can timestamp EVIN detections independently from the latched `EVF` flag. `event_timestamp` publishes each recorded RTC timestamp as an ISO8601 UTC text sensor with `device_class: timestamp`, and Home Assistant Recorder keeps the history as the event log.

`timestamp_record_mode` controls how the hardware stores pending records before ESPHome drains them:
- `latest`: store only the newest event in registers `0x20`–`0x29`
- `stop_when_full`: store up to 8 records in timestamp RAM, then stop until RAM is cleared
- `overwrite`: store up to 8 records in timestamp RAM and wrap when full

In buffered modes, the component publishes stored timestamps oldest-to-newest and then clears the timestamp RAM pointer so capture can continue. This does **not** clear `EVF`; `rx8xxx.clear_event_flag` remains manual.

#### RX8130CE

```yaml
    digital_offset: 0          # -64 to 63 (each step ≈ 3.05 ppm)
```

Applies a digital frequency offset correction to the 32.768 kHz oscillator. The 7-bit two's complement field provides a range of -195.31 ppm to +192.26 ppm in ~3.05 ppm steps. Correction is applied every 10 seconds to the sub-second clocks (does not affect 32.768 kHz FOUT output).

---

## Automation

### Triggers

```yaml
    on_alarm:
      - logger.log: "Alarm triggered!"

    on_timer:
      - logger.log: "Timer triggered!"

    on_event:
      - logger.log: "EVIN event flag triggered!"
```

| Trigger | Description |
|---------|-------------|
| `on_alarm` | Fires when the Alarm Flag (AF) transitions from 0 to 1 (rising edge only) |
| `on_timer` | Fires when the Timer Flag (TF) transitions from 0 to 1 (rising edge only) |
| `on_event` | Fires when the Event Flag (EVF) transitions from 0 to 1 (rising edge only, RX8111CE only) |

Callbacks fire **once per event**, not repeatedly while the flag remains set. The flag must be cleared (via the clear actions below) before the trigger can fire again on the next event.

### Actions

#### Read / Write Time

```yaml
# Read time from RTC hardware into the ESPHome system clock
- rx8xxx.read_time:
    id: my_rtc

# Write the ESPHome system clock to the RTC hardware
- rx8xxx.write_time:
    id: my_rtc
```

#### Clear Flags

```yaml
# Clear the Alarm Flag (AF) and release the /INT pin
- rx8xxx.clear_alarm_flag:
    id: my_rtc

# Clear the Timer Flag (TF) to re-arm on_timer edge detection
- rx8xxx.clear_timer_flag:
    id: my_rtc

# Clear the Event Flag (EVF) and release /INT from an EVIN event
- rx8xxx.clear_event_flag:
    id: my_rtc
```

#### Set Alarm (Runtime)

Reprogram the alarm registers at runtime. Omit fields to disable matching on that field (the register is set to don't-care).

```yaml
- rx8xxx.set_alarm:
    id: my_rtc
    enabled: true
    second: 30         # or "seconds" (alias)
    minute: 0          # or "minutes" (alias)
    hour: 12           # or "hours" (alias)
    weekday: 3         # or "weekdays" (alias). 1=Sun … 7=Sat
    day: 15            # or "days" (alias). 1–31
```

**Rules:**
- Cannot specify both `weekday` and `day` (mutually exclusive).
- Cannot use both singular and plural forms of the same field (e.g., `minute` and `minutes`).
- At least one time field or `enabled` must be specified.

#### Schedule Alarm In (Relative)

Schedule an alarm at a time offset from now. The component calculates the absolute target time and programs the alarm registers. This is not a base function of these RTCs, and is implemented via software by calculating time offsets, thus alarms will not be entirely precise to the second.

```yaml
- rx8xxx.schedule_alarm_in:
    id: my_rtc
    seconds: 30        # or "second" (alias)
    minutes: 5         # or "minute" (alias)
    hours: 1           # or "hour" (alias)
    days: 2            # or "day" (alias)
```

**Rules:**
- Total duration must be between 1 and 2,419,200 seconds (**28 days maximum**).
- The alarm must already be enabled (via config `alarm_enabled: true` or `rx8xxx.set_alarm`).
- The system clock must be valid (synced from Home Assistant or read from the RTC).
- Uses day-of-month matching (not weekday) for the computed target.
- On RX8111CE, the per-second alarm field is automatically programmed from the target time. On RX8130CE, only minute/hour/day fields are used.

> **Why 28 days?** The alarm hardware has no month register — it matches only on day-of-month, hour, minute, and second. Durations beyond 28 days (the shortest month) risk the day-of-month matching in the wrong month. For example, scheduling 31 days from January 31 computes a target of March 3, but the alarm registers would be set to day=3 — and the RTC would fire on February 3 instead since that is the next date that Day = 3 on the calendar.

---

## Important Notes and Gotchas

### Using the RTC as an ultra low power deep sleep timer

The RTC /INT pin can be used in conjunction with the alarm function (and suitable external power control circuitry) to simulate an ultra low power deep sleep state. However, a hard power cut will result in the device dropping off the Home Assistant network without warning, and thus Home Assistant will report the device as "unavailable". To prevent this, a deep_sleep component must be set, and the device needs to communicate to the Home Assistant server that the disconnect was intentional. See the example YAML for a demo implementation, paying attention to the lambda which initiates a clean network disconnect.

### Alarm /INT Pin Behavior

- When the alarm fires, the chip **holds the /INT pin LOW indefinitely**.
- The /INT pin is **only released** when you call `rx8xxx.clear_alarm_flag`.
- This is by design — it allows the /INT pin to drive a power switch (e.g., PMOS FET) that keeps the MCU powered until it explicitly acknowledges the alarm.
- **Reconfiguring the alarm does NOT clear an existing Alarm Flag.** If AF is set, it stays set regardless of alarm reconfiguration.

### Timer /INT Pin Behavior

- When the timer fires, the /INT pin **auto-releases after ~7.8 ms** (hardware behavior, not software-controlled).
- By the time the `on_timer` callback runs (on the next poll), /INT is already HIGH again.
- The Timer Flag (TF) **stays latched** until `rx8xxx.clear_timer_flag` is called.
- Calling `clear_timer_flag` has **no effect on the /INT pin** — it only re-arms the edge detection so the next TF 0→1 transition fires `on_timer` again.

### Flag Behavior Summary

| Flag | Auto-cleared? | /INT behavior | Clear action |
|------|--------------|---------------|--------------|
| AF (Alarm) | No | Held LOW until cleared | `rx8xxx.clear_alarm_flag` |
| TF (Timer) | No | Auto-releases ~7.8 ms | `rx8xxx.clear_timer_flag` |
| EVF (Event, RX8111CE) | No | Held LOW until cleared if `EIE=1` | `rx8xxx.clear_event_flag` |

These flags are **preserved across power cycles and reconfigurations**. On boot, startup flags are cleared (RX8111CE: VLF, XST, POR; RX8130CE: VLF, RSF), but AF, TF, and EVF are intentionally preserved so events that occurred while the MCU was off can be detected on the next poll.

### Polling and Edge Detection

- Flags are read every `update_interval`.
- `on_alarm`, `on_timer`, and `on_event` fire only on the **0→1 rising edge** — once per event, not on every poll while the flag is set.
- To receive the next event's callback, you **must clear the flag** after handling the current one.
- `event_timestamp` publishing is separate from `EVF`; buffered timestamp RAM is drained automatically after publish, but `EVF` still stays latched until you clear it.

### Weekday Encoding

- User-facing values: `1`=Sunday, `2`=Monday, … `7`=Saturday.
- Internally stored as one-hot encoding in hardware (bit 0 = Sunday, bit 6 = Saturday). Conversion is automatic.

### Time Storage

- All time fields are stored in BCD format in hardware, covering years 2000–2099.
- The component converts between BCD and decimal automatically.
- During I2C reads, the chip freezes its counter chain so all 7 time bytes form a coherent snapshot.
- During writes, the STOP bit halts timekeeping to prevent partial-update glitches.

### Control Register Safety

- The component always masks bit 7 of the control register to 0 on every write. This prevents accidentally setting the TEST bit (RX8130CE), which must never be set to 1 in normal operation.

---

## Compile-Time Validation

The component validates your configuration at compile time and produces clear error messages:

| Rule | Error |
|------|-------|
| `battery_charging: true` without `battery_backup: true` | `'battery_charging' requires 'battery_backup: true'` |
| `timer_clock: "1_3600hz"` on RX8111CE | `Timer clock '1_3600hz' is only available on the RX8130CE` |
| `timer_count` exceeds model limit | Error with model name, bit width, and max value |
| `timer_enabled: true` without `timer_count` | `'timer_enabled: true' requires 'timer_count'` |
| `alarm_second` on RX8130CE | `'alarm_second' is only available on the RX8111CE` |
| `timestamp_enabled` on RX8130CE | `'timestamp_enabled' is only available on the RX8111CE` |
| `timestamp_record_mode` on RX8130CE | `'timestamp_record_mode' is only available on the RX8111CE` |
| `event_timestamp` on RX8130CE | `'event_timestamp' text sensor is only available on the RX8111CE` |
| `event_level` on RX8130CE | `'event_level' is only available on the RX8111CE` |
| `evin_filter` on RX8130CE | `'evin_filter' is only available on the RX8111CE` |
| `evin_pull` on RX8130CE | `'evin_pull' is only available on the RX8111CE` |
| `xst`, `evin`, or `event_flag` sensor on RX8130CE | Sensor only available on RX8111CE |
| `digital_offset` on RX8111CE | `'digital_offset' is only available on the RX8130CE` |
| `vblf` sensor on RX8111CE | Sensor only available on RX8130CE |
| Both `alarm_weekday` and `alarm_day` | Mutually exclusive fields error |

---

## Full Example

```yaml
esphome:
  name: my-rtc-device

esp32:
  board: esp32dev

i2c:
  sda: GPIO23
  scl: GPIO22
  frequency: 400kHz

external_components:
  - source:
      type: git
      url: https://github.com/tech_dregs/RX8XXX
    components: [rx8xxx]

globals:
  - id: cycle_fired
    type: bool
    restore_value: no
    initial_value: "false"

time:
  - platform: rx8xxx
    id: rtc
    model: RX8111CE
    update_interval: 2s

    # Battery
    battery_backup: true
    battery_charging: false

    # Clock output
    fout_frequency: "1hz"

    # Alarm: fire at :30 every minute
    alarm_second: 30

    on_alarm:
      - logger.log: "Alarm fired!"
      # IMPORTANT: clear the flag to release /INT and re-arm for next event
      - rx8xxx.clear_alarm_flag:
          id: rtc

    # Timer: fire every 60 seconds
    timer_count: 60
    timer_clock: "1hz"

    on_timer:
      - logger.log: "Timer fired!"
      # Clear TF to re-arm edge detection for the next event
      - rx8xxx.clear_timer_flag:
          id: rtc

    # Status sensors
    vlf:
      name: "RTC Voltage Low"
    alarm_flag:
      name: "RTC Alarm Flag"
    timer_flag:
      name: "RTC Timer Flag"
    xst:
      name: "RTC Crystal Stop"
    battery_low:
      name: "RTC Battery Low"
    evin:
      name: "RTC Event Input"

    timestamp_enabled: true
    timestamp_record_mode: stop_when_full

    event_timestamp:
      name: "RTC Event Timestamp"

    event_level: low
    evin_filter: 2              # 15.6ms chattering filter
    evin_pull: pullup_500k      # Internal pull-up (pin high at idle)

    on_event:
      - logger.log: "EVIN event captured!"
      # EVF remains latched until cleared separately.
      - rx8xxx.clear_event_flag:
          id: rtc

  # Sync time from Home Assistant
  - platform: homeassistant
    id: ha_time
    on_time_sync:
      - rx8xxx.write_time:
          id: rtc

# Buttons for manual control
button:
  - platform: template
    name: "Sync Time to RTC"
    on_press:
      - rx8xxx.write_time:
          id: rtc

  - platform: template
    name: "Read Time from RTC"
    on_press:
      - rx8xxx.read_time:
          id: rtc

  - platform: template
    name: "Clear Alarm Flag"
    on_press:
      - rx8xxx.clear_alarm_flag:
          id: rtc

  - platform: template
    name: "Clear Timer Flag"
    on_press:
      - rx8xxx.clear_timer_flag:
          id: rtc

  - platform: template
    name: "Schedule Alarm in 5 Minutes"
    on_press:
      - rx8xxx.schedule_alarm_in:
          id: rtc
          minutes: 5
    
  - platform: template
    name: "Test Alarm"
    on_press:
      - logger.log: "Test alarm started."

deep_sleep:
  id: deep_sleep_dummy #Prevents HA from showing "unavailable"

script:
  - id: pmos_cycle
    mode: restart
    then:
      - logger.log: "Power cut routine started."
      - rx8xxx.read_time:
          id: rtc
      #- delay: 10ms #add delay if alarm isn't setting.
      - rx8xxx.schedule_alarm_in:
          id: rtc
          seconds: 15 #seconds only available on RX8111
      # Lambda to simulate normal device disconnect. Prevents HA from showing "unavailable".
      - lambda: |-
          using namespace esphome::api;

          if (global_api_server != nullptr) {
            global_api_server->on_shutdown();  // initiate polite disconnect

            // Give it a short window to flush disconnect + close connections.
            uint32_t start = millis();
            while (millis() - start < 500) {
              global_api_server->loop();       // pump network I/O
              if (global_api_server->teardown())  // true when finished
                break;
              delay(10);
            }
          }
      - logger.log: "Alarm scheduled, waiting before clear"
      #- delay: 10ms #add delay if alarm isn't setting.
      - logger.log: "Cutting Power"
      - rx8xxx.clear_alarm_flag:
          id: rtc
      - delay: 100ms
      - logger.log: "Warning: Power appears to still be on."

interval:
  - interval: 1s
    startup_delay:
      seconds: 30
    then:
    - if:
        condition:
          lambda: 'return !id(cycle_fired);'
        then:
          - globals.set:
              id: cycle_fired
              value: "true"
          - script.execute: pmos_cycle
```

## Example YAML Files

The [`Example_YAML/`](Example_YAML/) folder contains focused, minimal configurations for each major feature:

| File | Description |
|------|-------------|
| [`Minimal_RTC.yaml`](Example_YAML/Minimal_RTC.yaml) | Basic timekeeping with Home Assistant sync |
| [`Minimal_Fixed_Alarm.yaml`](Example_YAML/Minimal_Fixed_Alarm.yaml) | Alarm at a fixed time (e.g., 07:30 daily) |
| [`Minimal_Relative_Alarm.yaml`](Example_YAML/Minimal_Relative_Alarm.yaml) | Schedule alarm N minutes/seconds from now |
| [`Minimal_Timer.yaml`](Example_YAML/Minimal_Timer.yaml) | Periodic countdown timer with `on_timer` |
| [`Minimal_Event.yaml`](Example_YAML/Minimal_Event.yaml) | EVIN event detection with timestamps, filter, and pull-up |
| [`Minimal_Status_Sensors.yaml`](Example_YAML/Minimal_Status_Sensors.yaml) | All binary sensors for RTC health monitoring |
| [`Minimal_Alarm_Deep_Sleep.yaml`](Example_YAML/Minimal_Alarm_Deep_Sleep.yaml) | Ultra-low-power wake cycle using RTC alarm + PMOS switch |
| [`Minimal_Event_Deep_Sleep.yaml`](Example_YAML/Minimal_Event_Deep_Sleep.yaml) | Ultra-low-power wake on EVIN event + PMOS switch |

---

## Dependencies

- **ESPHome** with `i2c` component configured
- `binary_sensor` and `text_sensor` are auto-loaded by the component

## License

See repository for license details.
