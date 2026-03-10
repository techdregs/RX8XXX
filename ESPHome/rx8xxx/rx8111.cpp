#include "rx8111.h"
#include "esphome/core/log.h"

#include <cstdio>

namespace esphome {
namespace rx8xxx {

static const char *const TAG = "rx8111";

namespace {

struct DecodedTimestampRecord {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint16_t milliseconds;
  bool vlow;
  bool vcmp;
  bool vdet;
  bool xst;
};

bool decode_bcd_(uint8_t value, uint8_t max_value, uint8_t *decoded) {
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

bool decode_timestamp_fields_(uint8_t sec_raw, uint8_t min_raw, uint8_t hour_raw, uint8_t day_raw, uint8_t month_raw,
                              uint8_t year_raw, uint8_t status_raw, uint16_t milliseconds,
                              DecodedTimestampRecord *record) {
  uint8_t second = 0;
  uint8_t minute = 0;
  uint8_t hour = 0;
  uint8_t day = 0;
  uint8_t month = 0;
  uint8_t year = 0;

  if (!decode_bcd_(sec_raw & 0x7F, 59, &second) || !decode_bcd_(min_raw & 0x7F, 59, &minute) ||
      !decode_bcd_(hour_raw & 0x3F, 23, &hour) || !decode_bcd_(day_raw & 0x3F, 31, &day) ||
      !decode_bcd_(month_raw & 0x1F, 12, &month) || !decode_bcd_(year_raw, 99, &year) || day == 0 || month == 0) {
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

bool decode_single_timestamp_record_(const std::array<uint8_t, 10> &raw, DecodedTimestampRecord *record) {
  const uint16_t fractional_1024 =
      static_cast<uint16_t>((static_cast<uint16_t>(raw[1]) << 2) | (raw[0] & 0x03));
  const uint16_t milliseconds = static_cast<uint16_t>((fractional_1024 * 1000U + 512U) / 1024U);

  return decode_timestamp_fields_(raw[2], raw[3], raw[4], raw[6], raw[7], raw[8], raw[9], milliseconds, record);
}

bool decode_buffered_timestamp_record_(const std::array<uint8_t, 8> &raw, DecodedTimestampRecord *record) {
  const uint16_t milliseconds = static_cast<uint16_t>((static_cast<uint16_t>(raw[0]) * 1000U + 128U) / 256U);
  return decode_timestamp_fields_(raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], milliseconds, record);
}

void format_timestamp_utc_(const DecodedTimestampRecord &record, char *buffer, size_t buffer_len) {
  std::snprintf(buffer, buffer_len, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", record.year, record.month, record.day,
                record.hour, record.minute, record.second, record.milliseconds);
}

const char *timestamp_record_mode_to_string_(TimestampRecordMode mode) {
  switch (mode) {
    case TIMESTAMP_RECORD_LATEST:
      return "latest";
    case TIMESTAMP_RECORD_STOP_WHEN_FULL:
      return "stop_when_full";
    case TIMESTAMP_RECORD_OVERWRITE:
      return "overwrite";
    default:
      return "unknown";
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// dump_config
// ---------------------------------------------------------------------------
void RX8111Component::dump_config() {
  this->RX8XXXComponent::dump_config();
  ESP_LOGCONFIG(TAG, "  Timestamp Capture: %s", ONOFF(this->timestamp_capture_active_()));
  if (this->timestamp_capture_active_()) {
    ESP_LOGCONFIG(TAG, "  Timestamp Record Mode: %s", timestamp_record_mode_to_string_(this->timestamp_record_mode_));
  }
  if (this->event_timestamp_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Event Timestamp Sensor: configured");
  }
  if (this->event_enabled_ || this->timestamp_capture_active_()) {
    static const char *const FILTER_NAMES[] = {"off", "3.9ms", "15.6ms", "125ms"};
    ESP_LOGCONFIG(TAG, "  EVIN Event Level: %s",
                  this->event_level_ == EVENT_LEVEL_HIGH ? "high" : "low");
    ESP_LOGCONFIG(TAG, "  EVIN Filter: %s (level %u)", FILTER_NAMES[this->evin_filter_], this->evin_filter_);
    const char *pull_str = "none (Hi-Z)";
    switch (this->evin_pull_) {
      case EVIN_PULL_UP_500K:    pull_str = "pull-up 500k"; break;
      case EVIN_PULL_UP_1M:      pull_str = "pull-up 1M"; break;
      case EVIN_PULL_UP_10M:     pull_str = "pull-up 10M"; break;
      case EVIN_PULL_DOWN_500K:  pull_str = "pull-down 500k"; break;
      default: break;
    }
    ESP_LOGCONFIG(TAG, "  EVIN Pull: %s", pull_str);
  }
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
// configure_event_
//
// Enables EIE so that a latched EVF pulls /INT low. EVIN level/filter/pull and
// timestamp mode bits are applied centrally from apply_runtime_options_().
// ---------------------------------------------------------------------------
bool RX8111Component::configure_event_() {
  uint8_t ctrl = 0;
  if (!this->read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl |= RX8111_CTRL_EIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// disable_event_
//
// Disables EIE without touching EVF. ETS and EVIN routing are handled by the
// runtime timestamp configuration so timestamp capture can stay active without
// asserting /INT.
// ---------------------------------------------------------------------------
bool RX8111Component::disable_event_() {
  uint8_t ctrl = 0;
  if (!this->read_byte(RX8111_REG_CONTROL, &ctrl)) return false;
  ctrl &= ~RX8111_CTRL_EIE;
  if (!this->write_byte(RX8111_REG_CONTROL, ctrl)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// apply_runtime_options_
//
// Applies RX8111-specific timestamp routing and EVIN settings. ETS must remain
// set if either event detection or timestamp capture is active.
// ---------------------------------------------------------------------------
bool RX8111Component::apply_runtime_options_() {
  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  if (this->timestamp_capture_active_() || this->event_enabled_) {
    ext |= RX8111_EXT_ETS;
  } else {
    ext &= ~RX8111_EXT_ETS;
  }

  if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  return this->write_timestamp_mode_bits_();
}

bool RX8111Component::timestamp_capture_active_() const {
  return this->timestamp_enabled_ || this->event_timestamp_sensor_ != nullptr;
}

bool RX8111Component::buffered_timestamp_mode_() const {
  return this->timestamp_record_mode_ != TIMESTAMP_RECORD_LATEST;
}

bool RX8111Component::write_timestamp_mode_bits_() {
  uint8_t tsctrl1 = 0;
  if (!this->read_byte(RX8111_REG_TSCTRL1, &tsctrl1)) return false;

  tsctrl1 &= ~(RX8111_TSCTRL1_EISEL | RX8111_TSCTRL1_TSCLR | RX8111_TSCTRL1_TSRAM);
  if (this->timestamp_capture_active_() && this->buffered_timestamp_mode_()) {
    tsctrl1 |= RX8111_TSCTRL1_TSRAM;
  }

  if (!this->write_byte(RX8111_REG_TSCTRL1, tsctrl1)) return false;

  uint8_t evin_reg = 0;
  if (this->event_enabled_ || this->timestamp_capture_active_()) {
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

    if (this->timestamp_capture_active_() && this->timestamp_record_mode_ == TIMESTAMP_RECORD_OVERWRITE) {
      evin_reg |= RX8111_EVIN_OVW;
    }
  }

  return this->write_byte(RX8111_REG_EVIN_SETTING, evin_reg);
}

bool RX8111Component::clear_timestamp_ram_() {
  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  const bool restore_ets = (ext & RX8111_EXT_ETS) != 0;
  if (restore_ets) {
    ext &= ~RX8111_EXT_ETS;
    if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  uint8_t tsctrl1 = 0;
  if (!this->read_byte(RX8111_REG_TSCTRL1, &tsctrl1)) return false;
  tsctrl1 |= RX8111_TSCTRL1_TSCLR;
  if (!this->write_byte(RX8111_REG_TSCTRL1, tsctrl1)) return false;

  if (restore_ets) {
    ext |= RX8111_EXT_ETS;
    if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  return true;
}

void RX8111Component::after_initial_time_read_() {
  // On startup, read any pre-existing timestamps unconditionally (pass 0xFF to bypass EVF check).
  if (this->event_timestamp_sensor_ != nullptr && !this->publish_timestamp_records_(0xFF)) {
    ESP_LOGW(TAG, "Failed to publish startup event timestamps");
  }
}

bool RX8111Component::publish_timestamp_records_(uint8_t flag_byte) {
  if (this->event_timestamp_sensor_ == nullptr) {
    return true;
  }

  // In latest mode, skip the read (and ETS toggle) if EVF isn't set and we
  // already have a cached timestamp — nothing new to publish.
  if (!this->buffered_timestamp_mode_()) {
    const bool evf_set = (flag_byte & RX8111_FLAG_EVF) != 0;
    if (!evf_set && this->has_last_latest_timestamp_) {
      return true;
    }
  } else {
    // In buffered mode, check TSEMP before touching ETS.  Reading TSCTRL3
    // does not require disabling ETS.
    uint8_t tsctrl3 = 0;
    if (!this->read_byte(RX8111_REG_TSCTRL3, &tsctrl3)) return false;
    if ((tsctrl3 & RX8111_TSCTRL3_TSEMP) != 0) {
      return true;  // buffer empty, nothing to do
    }
  }

  uint8_t ext = 0;
  if (!this->read_byte(RX8111_REG_EXTENSION, &ext)) return false;

  const bool restore_ets = (ext & RX8111_EXT_ETS) != 0;
  if (restore_ets) {
    ext &= ~RX8111_EXT_ETS;
    if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  bool ok = true;
  if (!this->buffered_timestamp_mode_()) {
    std::array<uint8_t, 10> raw{};
    if (!this->read_bytes(RX8111_REG_TIMESTAMP_SINGLE, raw.data(), raw.size())) {
      ok = false;
    } else if (!this->has_last_latest_timestamp_ || raw != this->last_latest_timestamp_raw_) {
      DecodedTimestampRecord record{};
      if (decode_single_timestamp_record_(raw, &record)) {
        char timestamp_value[32];
        format_timestamp_utc_(record, timestamp_value, sizeof(timestamp_value));
        this->event_timestamp_sensor_->publish_state(timestamp_value);
        this->last_latest_timestamp_raw_ = raw;
        this->has_last_latest_timestamp_ = true;
        ESP_LOGD(TAG, "Published latest event timestamp %s (VLOW=%d VCMP=%d VDET=%d XST=%d)", timestamp_value,
                 record.vlow, record.vcmp, record.vdet, record.xst);
      } else {
        this->last_latest_timestamp_raw_ = raw;
        this->has_last_latest_timestamp_ = true;
        ESP_LOGW(TAG, "Skipping invalid latest timestamp record");
      }
    }
  } else {
    uint8_t tsctrl3 = 0;
    if (!this->read_byte(RX8111_REG_TSCTRL3, &tsctrl3)) {
      ok = false;
    } else if ((tsctrl3 & RX8111_TSCTRL3_TSEMP) == 0) {
      const bool is_full = (tsctrl3 & RX8111_TSCTRL3_TSFULL) != 0;
      const uint8_t latest_index = tsctrl3 & RX8111_TSCTRL3_TSAD_MASK;
      const uint8_t record_count = is_full ? 8 : static_cast<uint8_t>(latest_index + 1);
      const uint8_t first_index = is_full ? static_cast<uint8_t>((latest_index + 1) & 0x07) : 0;

      for (uint8_t offset = 0; offset < record_count; offset++) {
        const uint8_t slot = static_cast<uint8_t>((first_index + offset) & 0x07);
        std::array<uint8_t, 8> raw{};
        const uint8_t base = static_cast<uint8_t>(RX8111_REG_TIMESTAMP_MULTI + slot * 8);
        if (!this->read_bytes(base, raw.data(), raw.size())) {
          ok = false;
          break;
        }

        DecodedTimestampRecord record{};
        if (!decode_buffered_timestamp_record_(raw, &record)) {
          ESP_LOGW(TAG, "Skipping invalid buffered timestamp record in slot %u", slot);
          continue;
        }

        char timestamp_value[32];
        format_timestamp_utc_(record, timestamp_value, sizeof(timestamp_value));
        this->event_timestamp_sensor_->publish_state(timestamp_value);
        ESP_LOGD(TAG, "Published buffered event timestamp %s from slot %u (VLOW=%d VCMP=%d VDET=%d XST=%d)",
                 timestamp_value, slot, record.vlow, record.vcmp, record.vdet, record.xst);
      }

      if (ok && !this->clear_timestamp_ram_()) {
        ok = false;
      }
    }
  }

  if (restore_ets) {
    ext |= RX8111_EXT_ETS;
    if (!this->write_byte(RX8111_REG_EXTENSION, ext)) return false;
  }

  return ok;
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

  if (this->event_timestamp_sensor_ != nullptr && !this->publish_timestamp_records_(flag_byte)) {
    ESP_LOGW(TAG, "Failed to publish event timestamp records");
  }
}

}  // namespace rx8xxx
}  // namespace esphome
