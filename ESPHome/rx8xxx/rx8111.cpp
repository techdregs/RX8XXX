#include "rx8111.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rx8xxx {

static const char *const TAG = "rx8111";

// ---------------------------------------------------------------------------
// dump_config
// ---------------------------------------------------------------------------
void RX8111Component::dump_config() {
  this->RX8XXXComponent::dump_config();
  ESP_LOGCONFIG(TAG, "  Time Stamp: %s", ONOFF(this->timestamp_enabled_));
  binary_sensor::log_binary_sensor(TAG, "  ", "XST (Crystal Stop)", this->xst_sensor_);
  binary_sensor::log_binary_sensor(TAG, "  ", "Battery Low (VLOW)", this->battery_low_sensor_);
  binary_sensor::log_binary_sensor(TAG, "  ", "EVIN State", this->evin_sensor_);
}

// ---------------------------------------------------------------------------
// check_and_clear_vlf_
//
// Reads the flag register (0x1E) and warns on:
//   - VLF (bit1): oscillation was lost / power failure; time data invalid.
//   - XST (bit0): crystal oscillation stop detected.
//   - POR (bit7): power-on reset occurred.
// Clears VLF, XST, and POR. Preserves AF, TF, EVF, UF so that update()
// polling can detect them. Returns false only on I2C failure.
// ---------------------------------------------------------------------------
bool RX8111Component::check_and_clear_vlf_() {
  uint8_t flags = 0;
  if (!this->read_byte(RX8111_REG_FLAG, &flags)) {
    ESP_LOGE(TAG, "Can't read flag register");
    return false;
  }

  if (flags & RX8111_FLAG_VLF) {
    ESP_LOGW(TAG, "VLF set - oscillation was lost or power failed. "
                  "Time data invalid; write a valid time before use.");
  }
  if (flags & RX8111_FLAG_XST) {
    ESP_LOGW(TAG, "XST set - crystal oscillation stop was detected.");
  }
  if (flags & RX8111_FLAG_POR) {
    ESP_LOGD(TAG, "POR set - power-on reset occurred (expected on first boot).");
  }

  // Clear startup flags only; preserve AF, TF, EVF, UF for update() polling.
  uint8_t cleared = flags & ~(RX8111_FLAG_VLF | RX8111_FLAG_XST | RX8111_FLAG_POR);
  if (!this->write_byte(RX8111_REG_FLAG, cleared)) {
    ESP_LOGE(TAG, "Can't write flag register");
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// configure_battery_backup_
//
// Power Switch Control register (0x32):
//   bit7 = CHGEN  (trickle charge for rechargeable backup)
//   bit6 = INIEN  (automatic switchover enable)
//   bits5:4 = unused (0)
//   bits3:2 = SWSEL1:SWSEL0 (switchover voltage; preserve existing)
//   bits1:0 = SMPT1:SMPT0   (detection timing; preserve existing)
// ---------------------------------------------------------------------------
bool RX8111Component::configure_battery_backup_() {
  uint8_t reg = 0;
  if (!this->read_byte(RX8111_REG_PWRSWITCH, &reg)) {
    ESP_LOGE(TAG, "Can't read power switch register");
    return false;
  }

  reg |= RX8111_PWR_INIEN;

  if (this->battery_charging_) {
    reg |= RX8111_PWR_CHGEN;
  } else {
    reg &= ~RX8111_PWR_CHGEN;
  }

  if (!this->write_byte(RX8111_REG_PWRSWITCH, reg)) {
    ESP_LOGE(TAG, "Can't write power switch register");
    return false;
  }

  ESP_LOGD(TAG, "Battery backup enabled (INIEN=1, CHGEN=%d)", this->battery_charging_ ? 1 : 0);
  return true;
}

// ---------------------------------------------------------------------------
// configure_alarm_
//
// Alarm registers:
//   0x17  MIN alarm  (AE bit7; BCD minutes 00-59)
//   0x18  HOUR alarm (AE bit7; BCD hours  00-23)
//   0x19  WEEK/DAY alarm (AE bit7; one-hot weekday OR BCD day, WADA selects)
//   0x2C  SEC alarm  (AE bit7; BCD seconds 00-59) - RX8111CE exclusive
//
// AE=1 means the field is disabled (don't-care).
// WADA bit (0x1D bit3): 0 = week alarm, 1 = day alarm.
//
// NOTE: This method does NOT clear the alarm flag (AF). The flag represents
// a prior alarm event that the user must explicitly clear via
// rx8xxx.clear_alarm_flag. Reconfiguring the alarm does not alter past events.
// ---------------------------------------------------------------------------
bool RX8111Component::configure_alarm_() {
  // Keep AIE asserted while updating alarm registers so runtime reprogramming
  // does not deassert /INT when AF is already latched.
  uint8_t ctrl = 0;
  if (!this->read_byte(RX8111_REG_CONTROL, &ctrl)) return false;

  // --- Second alarm (0x2C) ---
  uint8_t sec_alarm = RX8XXX_ALARM_AE;  // AE=1 -> disabled
  if (this->alarm_second_ != 0xFF) {
    sec_alarm = uint8_to_bcd_(this->alarm_second_);  // AE=0 -> enabled
  }
  if (!this->write_byte(RX8111_REG_SEC_ALARM, sec_alarm)) return false;

  // --- Minute alarm (0x17) ---
  uint8_t min_alarm = RX8XXX_ALARM_AE;
  if (this->alarm_minute_ != 0xFF) {
    min_alarm = uint8_to_bcd_(this->alarm_minute_);
  }
  if (!this->write_byte(RX8XXX_REG_MIN_ALARM, min_alarm)) return false;

  // --- Hour alarm (0x18) ---
  uint8_t hour_alarm = RX8XXX_ALARM_AE;
  if (this->alarm_hour_ != 0xFF) {
    hour_alarm = uint8_to_bcd_(this->alarm_hour_);
  }
  if (!this->write_byte(RX8XXX_REG_HOUR_ALARM, hour_alarm)) return false;

  // --- Week / Day alarm (0x19) and WADA bit ---
  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  uint8_t wd_alarm = RX8XXX_ALARM_AE;
  if (this->alarm_weekday_ != 0xFF) {
    // WEEK alarm mode: one-hot weekday, WADA=0
    wd_alarm = weekday_to_onehot_(this->alarm_weekday_);
    ext &= ~RX8111_EXT_WADA;
  } else if (this->alarm_day_ != 0xFF) {
    // DAY alarm mode: BCD day, WADA=1
    wd_alarm = uint8_to_bcd_(this->alarm_day_);
    ext |= RX8111_EXT_WADA;
  }
  if (!this->write_byte(RX8XXX_REG_WD_ALARM, wd_alarm)) return false;
  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;

  // Ensure AIE is enabled after programming. If already enabled, this is a no-op.
  ctrl |= RX8111_CTRL_AIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// configure_timer_
//
// Timer counter is 24-bit: 0x1A (LSB), 0x1B, 0x1C (MSB).
// Source clock: TSEL1:TSEL0 in extension register 0x1D bits 1:0.
// TE (0x1D bit4) starts the timer. TIE (0x1F bit4) enables interrupt.
//
// Per datasheet: clear TE first, write counter, then set TE.
//
// NOTE: This method does NOT clear the timer flag (TF). Reconfiguring the
// timer (e.g. changing the count) leaves past events intact for the user to
// acknowledge via rx8xxx.clear_timer_flag.
// ---------------------------------------------------------------------------
bool RX8111Component::configure_timer_() {
  // Step 1: stop timer and disable interrupt while reconfiguring.
  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8111_EXT_TE;
  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;

  uint8_t ctrl = 0;
  if (!this->read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_TIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  // Step 2: write 24-bit counter (LSB first).
  uint32_t count = this->timer_count_;
  uint8_t timer_buf[3] = {
      static_cast<uint8_t>(count & 0xFF),
      static_cast<uint8_t>((count >> 8) & 0xFF),
      static_cast<uint8_t>((count >> 16) & 0xFF),
  };
  if (!this->write_bytes(RX8XXX_REG_TIMER0, timer_buf, sizeof(timer_buf))) return false;

  // Step 3: set source clock. 1/3600Hz is not supported; clamp to 1/60Hz.
  uint8_t tsel = static_cast<uint8_t>(this->timer_clock_);
  if (tsel > 3) {
    ESP_LOGW(TAG, "1/3600Hz timer clock not supported on RX8111CE; using 1/60Hz");
    tsel = 3;
  }
  ext &= ~RX8111_EXT_TSEL_MASK;
  ext |= (tsel & RX8111_EXT_TSEL_MASK);
  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;

  // Step 4: enable interrupt and start timer.
  ctrl |= RX8111_CTRL_TIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  ext |= RX8111_EXT_TE;
  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// disable_alarm_
//
// Disables alarm matching and alarm interrupt generation without touching AF.
// ---------------------------------------------------------------------------
bool RX8111Component::disable_alarm_() {
  uint8_t ctrl = 0;
  if (!this->read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_AIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  if (!this->write_byte(RX8111_REG_SEC_ALARM, RX8XXX_ALARM_AE)) return false;
  if (!this->write_byte(RX8XXX_REG_MIN_ALARM, RX8XXX_ALARM_AE)) return false;
  if (!this->write_byte(RX8XXX_REG_HOUR_ALARM, RX8XXX_ALARM_AE)) return false;
  if (!this->write_byte(RX8XXX_REG_WD_ALARM, RX8XXX_ALARM_AE)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// disable_timer_
//
// Disables timer interrupt (TIE=0) and timer engine (TE=0) without touching TF.
// ---------------------------------------------------------------------------
bool RX8111Component::disable_timer_() {
  uint8_t ctrl = 0;
  if (!this->read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_TIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8111_EXT_TE;
  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// apply_runtime_options_
//
// Applies RX8111-specific runtime options. Currently only timestamp engine.
// ---------------------------------------------------------------------------
bool RX8111Component::apply_runtime_options_() {
  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  if (this->timestamp_enabled_) {
    ext |= RX8111_EXT_ETS;
  } else {
    ext &= ~RX8111_EXT_ETS;
  }

  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// update_chip_binary_sensors_
//
// Reads the status monitor register (0x33) to get EVIN and VLOW states.
// XST is available directly from the flag_byte already read by update().
// ---------------------------------------------------------------------------
void RX8111Component::update_chip_binary_sensors_(uint8_t flag_byte) {
  // XST is bit0 of the flag register (already read by the base update()).
  if (this->xst_sensor_ != nullptr) {
    this->xst_sensor_->publish_state((flag_byte & RX8111_FLAG_XST) != 0);
  }

  // EVIN and VLOW require a separate read of the status monitor register (0x33).
  if (this->evin_sensor_ != nullptr || this->battery_low_sensor_ != nullptr) {
    uint8_t status = 0;
    if (this->read_byte(RX8111_REG_STATUS, &status)) {
      if (this->evin_sensor_ != nullptr) {
        this->evin_sensor_->publish_state((status & RX8111_STATUS_EVIN) != 0);
      }
      if (this->battery_low_sensor_ != nullptr) {
        this->battery_low_sensor_->publish_state((status & RX8111_STATUS_VLOW) != 0);
      }
    } else {
      ESP_LOGW(TAG, "Failed to read status monitor register");
    }
  }
}

}  // namespace rx8xxx
}  // namespace esphome
