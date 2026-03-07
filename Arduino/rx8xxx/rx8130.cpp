// Platform-independent RX8XXX RTC library — RX8130CE implementation

#include "rx8130.h"

namespace rx8xxx {

static const char *const TAG = "rx8130";

// ---------------------------------------------------------------------------
// check_and_clear_vlf
// ---------------------------------------------------------------------------
bool RX8130::check_and_clear_vlf() {
  uint8_t flags = 0;
  if (!this->i2c_read_byte(RX8130_REG_FLAG, &flags)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't read flag register");
    return false;
  }

  if (flags & RX8130_FLAG_VLF) {
    RX8_LOG(RX8_LOG_WARN, TAG, "VLF set - oscillation was lost or power failed. "
                                "Time data invalid; write a valid time before use.");
  }
  if (flags & RX8130_FLAG_VBFF) {
    RX8_LOG(RX8_LOG_WARN, TAG, "VBFF set - backup battery (VBAT) voltage is critically low.");
  }
  if (flags & RX8130_FLAG_VBLF) {
    RX8_LOG(RX8_LOG_WARN, TAG, "VBLF set - VDD supply battery level is low.");
  }

  // Clear VLF and RSF only. Preserve AF, TF, UF for poll_flags().
  // VBFF and VBLF remain for status reporting.
  uint8_t cleared = flags & ~(RX8130_FLAG_VLF | RX8130_FLAG_RSF);
  if (!this->i2c_write_byte(RX8130_REG_FLAG, cleared)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't write flag register");
    return false;
  }

  // Apply digital offset if configured.
  if (this->use_digital_offset_) {
    // Register 0x30: bit7 = DTE (offset enable), bits6:0 = signed offset L7..L1.
    uint8_t offset_reg = 0x80 | (static_cast<uint8_t>(this->digital_offset_) & 0x7F);
    if (!this->i2c_write_byte(RX8130_REG_DIGOFFSET, offset_reg)) {
      RX8_LOG(RX8_LOG_WARN, TAG, "Can't write digital offset register");
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// configure_battery_backup
// ---------------------------------------------------------------------------
bool RX8130::configure_battery_backup() {
  uint8_t ctrl1 = 0;
  if (!this->i2c_read_byte(RX8130_REG_CONTROL1, &ctrl1)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't read control register 1");
    return false;
  }

  ctrl1 |= RX8130_CTRL1_INIEN;

  if (this->battery_charging_) {
    ctrl1 |= RX8130_CTRL1_CHGEN;
  } else {
    ctrl1 &= ~RX8130_CTRL1_CHGEN;
  }

  if (!this->i2c_write_byte(RX8130_REG_CONTROL1, ctrl1)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't write control register 1");
    return false;
  }

  RX8_LOG(RX8_LOG_DEBUG, TAG, "Battery backup enabled (INIEN=1, CHGEN=%d)",
          this->battery_charging_ ? 1 : 0);
  return true;
}

// ---------------------------------------------------------------------------
// configure_alarm
// ---------------------------------------------------------------------------
bool RX8130::configure_alarm() {
  // Keep AIE asserted while updating alarm registers so runtime reprogramming
  // does not deassert /IRQ when AF is already latched.
  uint8_t ctrl0 = 0;
  if (!this->i2c_read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;

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
  if (!this->i2c_read_byte(RX8130_REG_EXTENSION, &ext)) return false;

  uint8_t wd_alarm = ALARM_AE;
  if (this->alarm_weekday_ != 0xFF) {
    // WEEK alarm mode: one-hot weekday, WADA=0
    wd_alarm = weekday_to_onehot(this->alarm_weekday_);
    ext &= ~RX8130_EXT_WADA;
  } else if (this->alarm_day_ != 0xFF) {
    // DAY alarm mode: BCD day, WADA=1
    wd_alarm = uint8_to_bcd(this->alarm_day_);
    ext |= RX8130_EXT_WADA;
  }
  if (!this->i2c_write_byte(REG_WD_ALARM, wd_alarm)) return false;
  if (!this->i2c_write_byte(RX8130_REG_EXTENSION, ext)) return false;

  // Ensure AIE is enabled after programming.
  ctrl0 |= RX8130_CTRL0_AIE;
  ctrl0 &= 0x7F;  // never set TEST bit
  if (!this->i2c_write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// configure_timer
// ---------------------------------------------------------------------------
bool RX8130::configure_timer() {
  // Step 1: stop timer and disable interrupt while reconfiguring.
  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8130_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8130_EXT_TE;
  if (!this->i2c_write_byte(RX8130_REG_EXTENSION, ext)) return false;

  uint8_t ctrl0 = 0;
  if (!this->i2c_read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;
  ctrl0 &= ~RX8130_CTRL0_TIE;
  ctrl0 &= 0x7F;  // never set TEST bit
  if (!this->i2c_write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  // Step 2: write 16-bit counter value; clamp to 16-bit max for this chip.
  uint32_t count = this->timer_count_;
  if (count > 0xFFFF) {
    RX8_LOG(RX8_LOG_WARN, TAG, "Timer count exceeds 16-bit max for RX8130CE; clamping to 65535");
    count = 0xFFFF;
  }
  uint8_t timer_buf[2] = {
      static_cast<uint8_t>(count & 0xFF),
      static_cast<uint8_t>((count >> 8) & 0xFF),
  };
  if (!this->i2c_write_bytes(REG_TIMER0, timer_buf, sizeof(timer_buf))) return false;

  // Step 3: set source clock.
  uint8_t tsel = static_cast<uint8_t>(this->timer_clock_);
  if (tsel > 4) tsel = 4;
  ext &= ~RX8130_EXT_TSEL_MASK;
  ext |= (tsel & RX8130_EXT_TSEL_MASK);
  if (!this->i2c_write_byte(RX8130_REG_EXTENSION, ext)) return false;

  // Step 4: enable interrupt and start timer.
  ctrl0 |= RX8130_CTRL0_TIE;
  ctrl0 &= 0x7F;  // never set TEST bit
  if (!this->i2c_write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  ext |= RX8130_EXT_TE;
  if (!this->i2c_write_byte(RX8130_REG_EXTENSION, ext)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// disable_alarm
// ---------------------------------------------------------------------------
bool RX8130::disable_alarm() {
  uint8_t ctrl0 = 0;
  if (!this->i2c_read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;
  ctrl0 &= ~RX8130_CTRL0_AIE;
  ctrl0 &= 0x7F;  // never set TEST bit
  if (!this->i2c_write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  if (!this->i2c_write_byte(REG_MIN_ALARM, ALARM_AE)) return false;
  if (!this->i2c_write_byte(REG_HOUR_ALARM, ALARM_AE)) return false;
  if (!this->i2c_write_byte(REG_WD_ALARM, ALARM_AE)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// disable_timer
// ---------------------------------------------------------------------------
bool RX8130::disable_timer() {
  uint8_t ctrl0 = 0;
  if (!this->i2c_read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;
  ctrl0 &= ~RX8130_CTRL0_TIE;
  ctrl0 &= 0x7F;  // never set TEST bit
  if (!this->i2c_write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8130_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8130_EXT_TE;
  if (!this->i2c_write_byte(RX8130_REG_EXTENSION, ext)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// read_chip_status
// ---------------------------------------------------------------------------
void RX8130::read_chip_status(uint8_t flag_byte) {
  // VBFF and VBLF are both in the flag register already read by poll_flags().
  this->vbff_ = (flag_byte & RX8130_FLAG_VBFF) != 0;
  this->vblf_ = (flag_byte & RX8130_FLAG_VBLF) != 0;
}

}  // namespace rx8xxx
