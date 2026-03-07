// Platform-independent RX8XXX RTC library — base class implementation
// Datasheets:
//   RX8111CE: Epson ETM61E application manual
//   RX8130CE: Epson ETM50E application manual

#include "rx8xxx_base.h"

namespace rx8xxx {

static const char *const TAG = "rx8xxx";

// ---------------------------------------------------------------------------
// Runtime alarm helpers
// ---------------------------------------------------------------------------
bool RX8xxxBase::set_alarm_runtime(uint8_t second, uint8_t minute, uint8_t hour,
                                   uint8_t weekday, uint8_t day, int8_t enabled_override) {
  if (weekday != 0xFF && day != 0xFF) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "set_alarm_runtime: cannot set both weekday and day");
    return false;
  }
  if (second != 0xFF && !this->supports_alarm_second_()) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "set_alarm_runtime: second alarm is not supported on this model");
    return false;
  }

  this->alarm_second_ = second;
  this->alarm_minute_ = minute;
  this->alarm_hour_ = hour;
  this->alarm_weekday_ = weekday;
  this->alarm_day_ = day;

  if (enabled_override >= 0) {
    this->alarm_enabled_ = enabled_override != 0;
  }

  if (this->alarm_enabled_) {
    return this->configure_alarm();
  }
  return true;
}

// ---------------------------------------------------------------------------
// configure_fout — shared by both chips (FSEL bits are identical)
// ---------------------------------------------------------------------------
bool RX8xxxBase::configure_fout() {
  uint8_t ext = 0;
  if (!this->i2c_read_byte(this->extension_reg_addr(), &ext)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't read extension register");
    return false;
  }
  ext &= ~EXT_FSEL_MASK;
  ext |= (static_cast<uint8_t>(this->fout_frequency_) << EXT_FSEL_SHIFT)
         & EXT_FSEL_MASK;
  if (!this->i2c_write_byte(this->extension_reg_addr(), ext)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't write extension register");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Flag clearing — shared by both chips via virtual register accessors
// ---------------------------------------------------------------------------
bool RX8xxxBase::clear_alarm_flag() {
  uint8_t flags = 0;
  if (!this->i2c_read_byte(this->flag_reg_addr(), &flags)) return false;
  flags &= ~this->alarm_flag_mask();
  return this->i2c_write_byte(this->flag_reg_addr(), flags);
}

bool RX8xxxBase::clear_timer_flag() {
  uint8_t flags = 0;
  if (!this->i2c_read_byte(this->flag_reg_addr(), &flags)) return false;
  flags &= ~this->timer_flag_mask();
  return this->i2c_write_byte(this->flag_reg_addr(), flags);
}

// ---------------------------------------------------------------------------
// begin()
// ---------------------------------------------------------------------------
bool RX8xxxBase::begin() {
  // 1. Read flags, warn on VLF/XST/POR, clear startup flags only.
  if (!this->check_and_clear_vlf()) {
    return false;
  }

  // 2. Configure battery backup (INIEN, optionally CHGEN).
  if (this->battery_backup_) {
    if (!this->configure_battery_backup()) {
      RX8_LOG(RX8_LOG_WARN, TAG, "Battery backup configuration failed");
    }
  }

  // 3. Configure FOUT clock output.
  if (!this->configure_fout()) {
    RX8_LOG(RX8_LOG_WARN, TAG, "FOUT configuration failed");
  }

  // 4. Configure alarm registers, or explicitly disable alarm logic if omitted.
  if (this->alarm_enabled_) {
    if (!this->configure_alarm()) {
      RX8_LOG(RX8_LOG_WARN, TAG, "Alarm configuration failed");
    }
  } else if (!this->disable_alarm()) {
    RX8_LOG(RX8_LOG_WARN, TAG, "Alarm disable failed");
  }

  // 5. Configure wake-up timer, or explicitly disable it if omitted.
  if (this->timer_enabled_) {
    if (!this->configure_timer()) {
      RX8_LOG(RX8_LOG_WARN, TAG, "Timer configuration failed");
    }
  } else if (!this->disable_timer()) {
    RX8_LOG(RX8_LOG_WARN, TAG, "Timer disable failed");
  }

  // 6. Apply optional model-specific runtime options (e.g. RX8111 timestamp).
  if (!this->apply_runtime_options()) {
    RX8_LOG(RX8_LOG_WARN, TAG, "Model-specific runtime options failed");
  }

  return true;
}

// ---------------------------------------------------------------------------
// poll_flags() - read flag register, detect edges, delegate to subclass
// ---------------------------------------------------------------------------
bool RX8xxxBase::poll_flags(RX8xxxFlags *out) {
  uint8_t flag_byte = 0;
  if (!this->i2c_read_byte(this->flag_reg_addr(), &flag_byte)) {
    RX8_LOG(RX8_LOG_WARN, TAG, "Failed to read flag register");
    return false;
  }

  const bool alarm_set = (flag_byte & this->alarm_flag_mask()) != 0;
  const bool timer_set = (flag_byte & this->timer_flag_mask()) != 0;
  const bool vlf_set   = (flag_byte & this->vlf_flag_mask()) != 0;

  out->vlf        = vlf_set;
  out->alarm_flag = alarm_set;
  out->timer_flag = timer_set;
  out->alarm_edge = alarm_set && !this->prev_alarm_flag_;
  out->timer_edge = timer_set && !this->prev_timer_flag_;

  this->prev_alarm_flag_ = alarm_set;
  this->prev_timer_flag_ = timer_set;

  // Let each subclass read its own chip-specific status.
  this->read_chip_status(flag_byte);

  return true;
}

// ---------------------------------------------------------------------------
// read_time() - read 0x10-0x16, convert to RX8xxxTime
// ---------------------------------------------------------------------------
bool RX8xxxBase::read_time(RX8xxxTime *out) {
  uint8_t buf[7];
  // Burst read from SEC register. During I2C access the chip automatically
  // freezes the counter chain, so all 7 bytes form a coherent snapshot.
  if (!this->i2c_read_bytes(REG_SEC, buf, sizeof(buf))) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't read time registers");
    return false;
  }

  out->second      = bcd_to_uint8(buf[0] & 0x7F);
  out->minute      = bcd_to_uint8(buf[1] & 0x7F);
  out->hour        = bcd_to_uint8(buf[2] & 0x3F);
  out->day_of_week = onehot_to_weekday(buf[3]);
  out->day         = bcd_to_uint8(buf[4] & 0x3F);
  out->month       = bcd_to_uint8(buf[5] & 0x1F);
  out->year        = static_cast<uint16_t>(bcd_to_uint8(buf[6]) + 2000);

  RX8_LOG(RX8_LOG_DEBUG, TAG, "Read %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
          out->year, out->month, out->day,
          out->hour, out->minute, out->second,
          out->day_of_week);

  return true;
}

// ---------------------------------------------------------------------------
// write_time() - freeze counter, write 0x10-0x16, restart counter
// ---------------------------------------------------------------------------
bool RX8xxxBase::write_time(const RX8xxxTime &time) {
  // Freeze the timekeeping counter while writing to prevent partial-update glitches.
  if (!this->set_stop_bit(true)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Failed to set STOP bit before time write");
    return false;
  }

  uint8_t buf[7];
  buf[0] = uint8_to_bcd(time.second);
  buf[1] = uint8_to_bcd(time.minute);
  buf[2] = uint8_to_bcd(time.hour);
  buf[3] = weekday_to_onehot(time.day_of_week);
  buf[4] = uint8_to_bcd(time.day);
  buf[5] = uint8_to_bcd(time.month);
  buf[6] = uint8_to_bcd(static_cast<uint8_t>((time.year - 2000) % 100));

  bool ok = this->i2c_write_bytes(REG_SEC, buf, sizeof(buf));

  // Always attempt to clear STOP, even if the write failed.
  if (!this->set_stop_bit(false)) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Failed to clear STOP bit after time write");
  }

  if (!ok) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "Can't write time registers");
    return false;
  }

  RX8_LOG(RX8_LOG_DEBUG, TAG, "Wrote %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
          time.year, time.month, time.day,
          time.hour, time.minute, time.second,
          time.day_of_week);

  return true;
}

// ---------------------------------------------------------------------------
// set_stop_bit() - read-modify-write the STOP bit in the control register.
// Bit7 of the control register is always masked to 0 to prevent writing the
// TEST bit (bit7) on the RX8130CE, which must never be set to 1.
// ---------------------------------------------------------------------------
bool RX8xxxBase::set_stop_bit(bool stop) {
  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(this->control_reg_addr(), &ctrl)) {
    return false;
  }
  if (stop) {
    ctrl |= this->stop_bit_mask();
  } else {
    ctrl &= ~this->stop_bit_mask();
  }
  ctrl &= 0x7F;  // never set bit7 (TEST bit on RX8130CE)
  return this->i2c_write_byte(this->control_reg_addr(), ctrl);
}

}  // namespace rx8xxx
