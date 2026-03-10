// Platform-independent RX8XXX RTC library — RX8111CE implementation

#include "rx8111.h"

namespace rx8xxx {

static const char *const TAG = "rx8111";

namespace {

bool decode_bcd(uint8_t value, uint8_t max_value, uint8_t *decoded) {
  const uint8_t ones = value & 0x0F;
  const uint8_t tens = (value >> 4) & 0x0F;
  if (ones > 9 || tens > 9) {
    return false;
  }

  const uint8_t result = tens * 10 + ones;
  if (result > max_value) {
    return false;
  }

  *decoded = result;
  return true;
}

bool decode_timestamp_fields(uint8_t sec_raw, uint8_t min_raw, uint8_t hour_raw, uint8_t day_raw, uint8_t month_raw,
                             uint8_t year_raw, uint8_t status_raw, uint16_t milliseconds,
                             RX8111TimestampRecord *record) {
  uint8_t second = 0;
  uint8_t minute = 0;
  uint8_t hour = 0;
  uint8_t day = 0;
  uint8_t month = 0;
  uint8_t year = 0;

  if (!decode_bcd(sec_raw & 0x7F, 59, &second) || !decode_bcd(min_raw & 0x7F, 59, &minute) ||
      !decode_bcd(hour_raw & 0x3F, 23, &hour) || !decode_bcd(day_raw & 0x3F, 31, &day) ||
      !decode_bcd(month_raw & 0x1F, 12, &month) || !decode_bcd(year_raw, 99, &year) || day == 0 || month == 0) {
    return false;
  }

  record->year = static_cast<uint16_t>(2000 + year);
  record->month = month;
  record->day = day;
  record->hour = hour;
  record->minute = minute;
  record->second = second;
  record->milliseconds = milliseconds;
  record->vlow = (status_raw & 0x20) != 0;
  record->vcmp = (status_raw & 0x10) != 0;
  record->vdet = (status_raw & 0x08) != 0;
  record->xst = (status_raw & 0x02) != 0;
  return true;
}

bool decode_single_timestamp_record(const uint8_t raw[10], RX8111TimestampRecord *record) {
  const uint16_t fractional_1024 =
      static_cast<uint16_t>((static_cast<uint16_t>(raw[1]) << 2) | (raw[0] & 0x03));
  const uint16_t milliseconds = static_cast<uint16_t>((fractional_1024 * 1000U + 512U) / 1024U);

  return decode_timestamp_fields(raw[2], raw[3], raw[4], raw[6], raw[7], raw[8], raw[9], milliseconds, record);
}

bool decode_buffered_timestamp_record(const uint8_t raw[8], RX8111TimestampRecord *record) {
  const uint16_t milliseconds = static_cast<uint16_t>((static_cast<uint16_t>(raw[0]) * 1000U + 128U) / 256U);
  return decode_timestamp_fields(raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], milliseconds, record);
}

}  // namespace

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
// configure_event
// ---------------------------------------------------------------------------
bool RX8111::configure_event() {
  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl |= RX8111_CTRL_EIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// disable_event
// ---------------------------------------------------------------------------
bool RX8111::disable_event() {
  uint8_t ctrl = 0;
  if (!this->i2c_read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_EIE;
  if (!this->i2c_write_byte(RX8111_REG_CONTROL, ctrl)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// apply_runtime_options
// ---------------------------------------------------------------------------
bool RX8111::apply_runtime_options() {
  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  if (this->timestamp_capture_active() || this->event_enabled_) {
    ext |= RX8111_EXT_ETS;
  } else {
    ext &= ~RX8111_EXT_ETS;
  }

  if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  return this->write_timestamp_mode_bits();
}

// ---------------------------------------------------------------------------
// timestamp_capture_active
// ---------------------------------------------------------------------------
bool RX8111::timestamp_capture_active() const {
  return this->timestamp_enabled_;
}

// ---------------------------------------------------------------------------
// buffered_timestamp_mode
// ---------------------------------------------------------------------------
bool RX8111::buffered_timestamp_mode() const {
  return this->timestamp_record_mode_ != TIMESTAMP_RECORD_LATEST;
}

// ---------------------------------------------------------------------------
// write_timestamp_mode_bits
// ---------------------------------------------------------------------------
bool RX8111::write_timestamp_mode_bits() {
  uint8_t tsctrl1 = 0;
  if (!this->i2c_read_byte(RX8111_REG_TSCTRL1, &tsctrl1)) return false;

  tsctrl1 &= ~(RX8111_TSCTRL1_EISEL | RX8111_TSCTRL1_TSCLR | RX8111_TSCTRL1_TSRAM);
  if (this->timestamp_capture_active() && this->buffered_timestamp_mode()) {
    tsctrl1 |= RX8111_TSCTRL1_TSRAM;
  }

  if (!this->i2c_write_byte(RX8111_REG_TSCTRL1, tsctrl1)) return false;

  uint8_t evin_reg = 0;
  if (this->event_enabled_ || this->timestamp_capture_active()) {
    if (this->event_level_ == EVENT_LEVEL_HIGH) {
      evin_reg |= RX8111_EVIN_EHL;
    }

    evin_reg |= (this->evin_filter_ << RX8111_EVIN_ET_SHIFT) & RX8111_EVIN_ET_MASK;

    switch (this->evin_pull_) {
      case EVIN_PULL_NONE:
        break;
      case EVIN_PULL_UP_500K:
        evin_reg |= RX8111_EVIN_PU0;
        break;
      case EVIN_PULL_UP_1M:
        evin_reg |= RX8111_EVIN_PU1;
        break;
      case EVIN_PULL_UP_10M:
        evin_reg |= RX8111_EVIN_PU1 | RX8111_EVIN_PU0;
        break;
      case EVIN_PULL_DOWN_500K:
        evin_reg |= RX8111_EVIN_PDN;
        break;
    }

    if (this->timestamp_capture_active() && this->timestamp_record_mode_ == TIMESTAMP_RECORD_OVERWRITE) {
      evin_reg |= RX8111_EVIN_OVW;
    }
  }

  return this->i2c_write_byte(RX8111_REG_EVIN_SETTING, evin_reg);
}

// ---------------------------------------------------------------------------
// clear_timestamp_ram
// ---------------------------------------------------------------------------
bool RX8111::clear_timestamp_ram() {
  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  const bool restore_ets = (ext & RX8111_EXT_ETS) != 0;
  if (restore_ets) {
    ext &= ~RX8111_EXT_ETS;
    if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  uint8_t tsctrl1 = 0;
  if (!this->i2c_read_byte(RX8111_REG_TSCTRL1, &tsctrl1)) return false;
  tsctrl1 |= RX8111_TSCTRL1_TSCLR;
  if (!this->i2c_write_byte(RX8111_REG_TSCTRL1, tsctrl1)) return false;

  if (restore_ets) {
    ext |= RX8111_EXT_ETS;
    if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// read_latest_event_timestamp
// ---------------------------------------------------------------------------
bool RX8111::read_latest_event_timestamp(RX8111TimestampRecord *out) {
  if (out == nullptr) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "read_latest_event_timestamp: output record is null");
    return false;
  }
  if (this->buffered_timestamp_mode()) {
    RX8_LOG(RX8_LOG_WARN, TAG, "read_latest_event_timestamp: timestamp_record_mode is not latest");
    return false;
  }

  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  const bool restore_ets = (ext & RX8111_EXT_ETS) != 0;
  if (restore_ets) {
    ext &= ~RX8111_EXT_ETS;
    if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  bool ok = false;
  uint8_t raw[10];
  if (this->i2c_read_bytes(RX8111_REG_TIMESTAMP_SINGLE, raw, sizeof(raw))) {
    ok = decode_single_timestamp_record(raw, out);
    if (!ok) {
      RX8_LOG(RX8_LOG_WARN, TAG, "read_latest_event_timestamp: timestamp data is invalid");
    }
  }

  if (restore_ets) {
    ext |= RX8111_EXT_ETS;
    if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  return ok;
}

// ---------------------------------------------------------------------------
// drain_buffered_event_timestamps
// ---------------------------------------------------------------------------
bool RX8111::drain_buffered_event_timestamps(RX8111TimestampRecord *records, uint8_t max_records, uint8_t *count_out) {
  if (records == nullptr || count_out == nullptr) {
    RX8_LOG(RX8_LOG_ERROR, TAG, "drain_buffered_event_timestamps: output buffer is null");
    return false;
  }
  *count_out = 0;

  if (!this->buffered_timestamp_mode()) {
    RX8_LOG(RX8_LOG_WARN, TAG, "drain_buffered_event_timestamps: timestamp_record_mode is latest");
    return false;
  }

  uint8_t ext = 0;
  if (!this->i2c_read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  const bool restore_ets = (ext & RX8111_EXT_ETS) != 0;
  if (restore_ets) {
    ext &= ~RX8111_EXT_ETS;
    if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  bool ok = true;
  uint8_t tsctrl3 = 0;
  if (!this->i2c_read_byte(RX8111_REG_TSCTRL3, &tsctrl3)) {
    ok = false;
  } else if ((tsctrl3 & RX8111_TSCTRL3_TSEMP) != 0) {
    *count_out = 0;
  } else {
    const bool is_full = (tsctrl3 & RX8111_TSCTRL3_TSFULL) != 0;
    const uint8_t latest_index = tsctrl3 & RX8111_TSCTRL3_TSAD_MASK;
    const uint8_t record_count = is_full ? 8 : static_cast<uint8_t>(latest_index + 1);
    const uint8_t first_index = is_full ? static_cast<uint8_t>((latest_index + 1) & 0x07) : 0;

    if (record_count > max_records) {
      RX8_LOG(RX8_LOG_WARN, TAG, "drain_buffered_event_timestamps: need %u records but max_records is %u",
              record_count, max_records);
      ok = false;
    } else {
      for (uint8_t offset = 0; offset < record_count; offset++) {
        const uint8_t slot = static_cast<uint8_t>((first_index + offset) & 0x07);
        const uint8_t base = static_cast<uint8_t>(RX8111_REG_TIMESTAMP_MULTI + slot * 8);
        uint8_t raw[8];
        if (!this->i2c_read_bytes(base, raw, sizeof(raw)) || !decode_buffered_timestamp_record(raw, &records[offset])) {
          RX8_LOG(RX8_LOG_WARN, TAG, "drain_buffered_event_timestamps: failed to decode slot %u", slot);
          ok = false;
          break;
        }
      }

      if (ok) {
        *count_out = record_count;
        if (!this->clear_timestamp_ram()) {
          ok = false;
          *count_out = 0;
        }
      }
    }
  }

  if (restore_ets) {
    ext |= RX8111_EXT_ETS;
    if (!this->i2c_write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  return ok;
}

// ---------------------------------------------------------------------------
// clear_timestamp_buffer
// ---------------------------------------------------------------------------
bool RX8111::clear_timestamp_buffer() {
  return this->clear_timestamp_ram();
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
