// Platform-independent RX8XXX RTC library — RX8111CE implementation

#include "rx8111.h"

namespace rx8xxx {

static const char *const TAG = "rx8111";

// ---------------------------------------------------------------------------
// check_and_clear_vlf
// ---------------------------------------------------------------------------
bool RX8111::check_and_clear_vlf() {
  uint8_t flags = 0;
  if (!this->i2c_read_byte(RX8111_REG_FLAG, &flags)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't read flag register");
    return false;
  }

  if (flags & RX8111_FLAG_VLF) {
    RX8_LOG(RX8_LOG_WARN, TAG, "VLF set - oscillation was lost or power failed. "
                                "Time data invalid; write a valid time before use.");
  }
  if (flags & RX8111_FLAG_XST) {
    RX8_LOG(RX8_LOG_WARN, TAG, "XST set - crystal oscillation stop was detected.");
  }
  if (flags & RX8111_FLAG_POR) {
    RX8_LOG(RX8_LOG_DEBUG, TAG, "POR set - power-on reset occurred (expected on first boot).");
  }

  // Clear startup flags only; preserve AF, TF, EVF, UF for poll_flags().
  uint8_t cleared = flags & ~(RX8111_FLAG_VLF | RX8111_FLAG_XST | RX8111_FLAG_POR);
  if (!this->i2c_write_byte(RX8111_REG_FLAG, cleared)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't write flag register");
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// configure_battery_backup
// ---------------------------------------------------------------------------
bool RX8111::configure_battery_backup() {
  uint8_t reg = 0;
  if (!this->i2c_read_byte(RX8111_REG_PWRSWITCH, &reg)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't read power switch register");
    return false;
  }

  reg |= RX8111_PWR_INIEN;

  if (this->battery_charging_) {
    reg |= RX8111_PWR_CHGEN;
  } else {
    reg &= ~RX8111_PWR_CHGEN;
  }

  if (!this->i2c_write_byte(RX8111_REG_PWRSWITCH, reg)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't write power switch register");
    return false;
  }

  RX8_LOG(RX8_LOG_DEBUG, TAG, "Battery backup enabled (INIEN=1, CHGEN=%d)",
          this->battery_charging_ ? 1 : 0);
  return true;
}

// ---------------------------------------------------------------------------
// configure_alarm
// ---------------------------------------------------------------------------
bool RX8111::configure_alarm() {
  // Keep AIE asserted while updating alarm registers so runtime reprogramming
  // does not deassert /INT when AF is already latched.
  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(RX8111_REG_CONTROL, &ctrl)) return false;

  // --- Second alarm (0x2C) ---
  uint8_t sec_alarm = ALARM_AE;  // AE=1 -> disabled
  if (this->alarm_second_ != 0xFF) {
    sec_alarm = uint8_to_bcd(this->alarm_second_);  // AE=0 -> enabled
  }
  if (!this->i2c_write_byte(RX8111_REG_SEC_ALARM, sec_alarm)) return false;

  // --- Minute alarm (0x17) ---
  uint8_t min_alarm = ALARM_AE;
  if (this->alarm_minute_ != 0xFF) {
    min_alarm = uint8_to_bcd(this->alarm_minute_);
  }
  if (!this->i2c_write_byte(REG_MIN_ALARM, min_alarm)) return false;

  // --- Hour alarm (0x18) ---
  uint8_t hour_alarm = ALARM_AE;
  if (this->alarm_hour_ != 0xFF) {
    hour_alarm = uint8_to_bcd(this->alarm_hour_);
  }
  if (!this->i2c_write_byte(REG_HOUR_ALARM, hour_alarm)) return false;

  // --- Week / Day alarm (0x19) and WADA bit ---
  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  uint8_t wd_alarm = ALARM_AE;
  if (this->alarm_weekday_ != 0xFF) {
    // WEEK alarm mode: one-hot weekday, WADA=0
    wd_alarm = weekday_to_onehot(this->alarm_weekday_);
    ext &= ~RX8111_EXT_WADA;
  } else if (this->alarm_day_ != 0xFF) {
    // DAY alarm mode: BCD day, WADA=1
    wd_alarm = uint8_to_bcd(this->alarm_day_);
    ext |= RX8111_EXT_WADA;
  }
  if (!this->i2c_write_byte(REG_WD_ALARM, wd_alarm)) return false;
  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;

  // Ensure AIE is enabled after programming.
  ctrl |= RX8111_CTRL_AIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// configure_timer
// ---------------------------------------------------------------------------
bool RX8111::configure_timer() {
  // Step 1: stop timer and disable interrupt while reconfiguring.
  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8111_EXT_TE;
  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;

  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_TIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  // Step 2: write 24-bit counter (LSB first).
  uint32_t count = this->timer_count_;
  uint8_t timer_buf[3] = {
      static_cast<uint8_t>(count & 0xFF),
      static_cast<uint8_t>((count >> 8) & 0xFF),
      static_cast<uint8_t>((count >> 16) & 0xFF),
  };
  if (!this->i2c_write_bytes(REG_TIMER0, timer_buf, sizeof(timer_buf))) return false;

  // Step 3: set source clock. 1/3600Hz is not supported; clamp to 1/60Hz.
  uint8_t tsel = static_cast<uint8_t>(this->timer_clock_);
  if (tsel > 3) {
    RX8_LOG(RX8_LOG_WARN, TAG, "1/3600Hz timer clock not supported on RX8111CE; using 1/60Hz");
    tsel = 3;
  }
  ext &= ~RX8111_EXT_TSEL_MASK;
  ext |= (tsel & RX8111_EXT_TSEL_MASK);
  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;

  // Step 4: enable interrupt and start timer.
  ctrl |= RX8111_CTRL_TIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  ext |= RX8111_EXT_TE;
  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// disable_alarm
// ---------------------------------------------------------------------------
bool RX8111::disable_alarm() {
  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_AIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  if (!this->i2c_write_byte(RX8111_REG_SEC_ALARM, ALARM_AE)) return false;
  if (!this->i2c_write_byte(REG_MIN_ALARM, ALARM_AE)) return false;
  if (!this->i2c_write_byte(REG_HOUR_ALARM, ALARM_AE)) return false;
  if (!this->i2c_write_byte(REG_WD_ALARM, ALARM_AE)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// disable_timer
// ---------------------------------------------------------------------------
bool RX8111::disable_timer() {
  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_TIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;

  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8111_EXT_TE;
  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// apply_runtime_options
// ---------------------------------------------------------------------------
bool RX8111::apply_runtime_options() {
  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  if (this->timestamp_enabled_) {
    ext |= RX8111_EXT_ETS;
  } else {
    ext &= ~RX8111_EXT_ETS;
  }

  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// read_chip_status
// ---------------------------------------------------------------------------
void RX8111::read_chip_status(uint8_t flag_byte) {
  // XST is bit0 of the flag register (already read by poll_flags).
  this->xst_ = (flag_byte & RX8111_FLAG_XST) != 0;

  // EVIN and VLOW require a separate read of the status monitor register (0x33).
  uint8_t status = 0;
  if (this->i2c_read_byte(RX8111_REG_STATUS, &status)) {
    this->evin_ = (status & RX8111_STATUS_EVIN) != 0;
    this->battery_low_ = (status & RX8111_STATUS_VLOW) != 0;
  } else {
    RX8_LOG(RX8_LOG_WARN, TAG, "Failed to read status monitor register");
  }
}

}  // namespace rx8xxx
