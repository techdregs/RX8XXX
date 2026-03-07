// Datasheets:
//   RX8111CE: Epson ETM61E application manual
//   RX8130CE: Epson ETM50E application manual

#include "rx8xxx.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rx8xxx {

static const char *const TAG = "rx8xxx";

// ---------------------------------------------------------------------------
// Runtime alarm helpers
// ---------------------------------------------------------------------------
bool RX8XXXComponent::set_alarm_runtime(uint8_t second, uint8_t minute, uint8_t hour,
                                        uint8_t weekday, uint8_t day, int8_t enabled_override) {
  if (weekday != 0xFF && day != 0xFF) {
    ESP_LOGE(TAG, "set_alarm_runtime: cannot set both weekday and day");
    return false;
  }
  if (second != 0xFF && !this->supports_alarm_second_()) {
    ESP_LOGE(TAG, "set_alarm_runtime: second alarm is not supported on this model");
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
    return this->configure_alarm_();
  }
  return true;
}

// ---------------------------------------------------------------------------
// configure_fout_ — shared by both chips (FSEL bits are identical)
// ---------------------------------------------------------------------------
bool RX8XXXComponent::configure_fout_() {
  uint8_t ext = 0;
  if (!this->read_byte(this->extension_reg_addr_(), &ext)) {
    ESP_LOGE(TAG, "Can't read extension register");
    return false;
  }
  ext &= ~RX8XXX_EXT_FSEL_MASK;
  ext |= (static_cast<uint8_t>(this->fout_frequency_) << RX8XXX_EXT_FSEL_SHIFT)
         & RX8XXX_EXT_FSEL_MASK;
  if (!this->write_byte(this->extension_reg_addr_(), ext)) {
    ESP_LOGE(TAG, "Can't write extension register");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Flag clearing — shared by both chips via virtual register accessors
// ---------------------------------------------------------------------------
bool RX8XXXComponent::clear_alarm_flag_() {
  uint8_t flags = 0;
  if (!this->read_byte(this->flag_reg_addr_(), &flags)) return false;
  flags &= ~this->alarm_flag_mask_();
  return this->write_byte(this->flag_reg_addr_(), flags);
}

bool RX8XXXComponent::clear_timer_flag_() {
  uint8_t flags = 0;
  if (!this->read_byte(this->flag_reg_addr_(), &flags)) return false;
  flags &= ~this->timer_flag_mask_();
  return this->write_byte(this->flag_reg_addr_(), flags);
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void RX8XXXComponent::setup() {
  // 1. Read flags, warn on VLF/XST/POR, clear startup flags only.
  if (!this->check_and_clear_vlf_()) {
    this->mark_failed();
    return;
  }

  // 2. Configure battery backup (INIEN, optionally CHGEN).
  if (this->battery_backup_) {
    if (!this->configure_battery_backup_()) {
      ESP_LOGW(TAG, "Battery backup configuration failed");
    }
  }

  // 3. Configure FOUT clock output.
  if (!this->configure_fout_()) {
    ESP_LOGW(TAG, "FOUT configuration failed");
  }

  // 4. Configure alarm registers, or explicitly disable alarm logic if omitted.
  if (this->alarm_enabled_) {
    if (!this->configure_alarm_()) {
      ESP_LOGW(TAG, "Alarm configuration failed");
    }
  } else if (!this->disable_alarm_()) {
    ESP_LOGW(TAG, "Alarm disable failed");
  }

  // 5. Configure wake-up timer, or explicitly disable it if omitted.
  if (this->timer_enabled_) {
    if (!this->configure_timer_()) {
      ESP_LOGW(TAG, "Timer configuration failed");
    }
  } else if (!this->disable_timer_()) {
    ESP_LOGW(TAG, "Timer disable failed");
  }

  // 6. Apply optional model-specific runtime options (e.g. RX8111 timestamp).
  if (!this->apply_runtime_options_()) {
    ESP_LOGW(TAG, "Model-specific runtime options failed");
  }

  // 7. Attempt an initial time read.
  this->read_time();
}

// ---------------------------------------------------------------------------
// update() - called periodically by the scheduler
//
// Design notes:
//   - AF (alarm flag) is NEVER auto-cleared here. AF holds /INT (or /IRQ) low
//     until the user explicitly calls rx8xxx.clear_alarm_flag, which both clears
//     the flag in software and releases the /INT pin to drive external circuits.
//   - TF (timer flag) is NOT auto-cleared here either, but note that the timer
//     /INT pin auto-releases in hardware after tRTN regardless of TF. Calling
//     rx8xxx.clear_timer_flag is only needed to re-arm the on_timer edge detection
//     for the next event — it has no effect on the /INT pin.
//   - on_alarm / on_timer callbacks fire only on the 0->1 rising edge so they
//     trigger exactly once per event, not on every poll while the flag stays set.
// ---------------------------------------------------------------------------
void RX8XXXComponent::update() {
  this->read_time();

  uint8_t flag_byte = 0;
  if (!this->read_byte(this->flag_reg_addr_(), &flag_byte)) {
    ESP_LOGW(TAG, "Failed to read flag register");
    return;
  }

  const bool alarm_set = (flag_byte & this->alarm_flag_mask_()) != 0;
  const bool timer_set = (flag_byte & this->timer_flag_mask_()) != 0;
  const bool vlf_set   = (flag_byte & this->vlf_flag_mask_()) != 0;

  // Publish common binary sensor states.
  if (this->vlf_sensor_ != nullptr)
    this->vlf_sensor_->publish_state(vlf_set);
  if (this->alarm_flag_sensor_ != nullptr)
    this->alarm_flag_sensor_->publish_state(alarm_set);
  if (this->timer_flag_sensor_ != nullptr)
    this->timer_flag_sensor_->publish_state(timer_set);

  // Fire trigger objects only on rising-edge transition (0->1).
  if (alarm_set && !this->prev_alarm_flag_)
    for (auto *t : this->alarm_triggers_) t->trigger();
  if (timer_set && !this->prev_timer_flag_)
    for (auto *t : this->timer_triggers_) t->trigger();

  this->prev_alarm_flag_ = alarm_set;
  this->prev_timer_flag_ = timer_set;

  // Let each subclass publish its own chip-specific binary sensors.
  this->update_chip_binary_sensors_(flag_byte);
}

// ---------------------------------------------------------------------------
// dump_config()
// ---------------------------------------------------------------------------
void RX8XXXComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "%s:", this->model_name_());
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
  }
  ESP_LOGCONFIG(TAG, "  Battery Backup: %s", ONOFF(this->battery_backup_));
  if (this->battery_backup_) {
    ESP_LOGCONFIG(TAG, "  Battery Charging: %s", ONOFF(this->battery_charging_));
  }
  const char *fout_str = "OFF";
  switch (this->fout_frequency_) {
    case FoutFrequency::FOUT_32768_HZ: fout_str = "32768 Hz"; break;
    case FoutFrequency::FOUT_1024_HZ:  fout_str = "1024 Hz";  break;
    case FoutFrequency::FOUT_1_HZ:     fout_str = "1 Hz";     break;
    case FoutFrequency::FOUT_OFF:      fout_str = "OFF";       break;
  }
  ESP_LOGCONFIG(TAG, "  FOUT Frequency: %s", fout_str);
  if (this->alarm_enabled_) {
    ESP_LOGCONFIG(TAG, "  Alarm: enabled");
  }
  if (this->timer_enabled_) {
    ESP_LOGCONFIG(TAG, "  Timer: enabled (count=%" PRIu32 ")", this->timer_count_);
  }
  LOG_BINARY_SENSOR("  ", "VLF",        this->vlf_sensor_);
  LOG_BINARY_SENSOR("  ", "Alarm Flag", this->alarm_flag_sensor_);
  LOG_BINARY_SENSOR("  ", "Timer Flag", this->timer_flag_sensor_);
  RealTimeClock::dump_config();
}

// ---------------------------------------------------------------------------
// read_time() - read 0x10-0x16, convert to ESPTime, sync system clock
// ---------------------------------------------------------------------------
void RX8XXXComponent::read_time() {
  uint8_t buf[7];
  // Burst read from SEC register. During I2C access the chip automatically
  // freezes the counter chain, so all 7 bytes form a coherent snapshot.
  if (!this->read_bytes(RX8XXX_REG_SEC, buf, sizeof(buf))) {
    ESP_LOGE(TAG, "Can't read time registers");
    return;
  }

  ESPTime rtc_time{
      .second       = bcd_to_uint8_(buf[0] & 0x7F),
      .minute       = bcd_to_uint8_(buf[1] & 0x7F),
      .hour         = bcd_to_uint8_(buf[2] & 0x3F),
      .day_of_week  = onehot_to_weekday_(buf[3]),
      .day_of_month = bcd_to_uint8_(buf[4] & 0x3F),
      .day_of_year  = 1,  // ignored by recalc_timestamp_utc(false)
      .month        = bcd_to_uint8_(buf[5] & 0x1F),
      .year         = static_cast<uint16_t>(bcd_to_uint8_(buf[6]) + 2000),
      .is_dst       = false,
      .timestamp    = 0,  // overwritten by recalc_timestamp_utc
  };
  rtc_time.recalc_timestamp_utc(false);

  if (!rtc_time.is_valid()) {
    ESP_LOGE(TAG, "Invalid RTC time, not syncing to system clock");
    return;
  }

  ESP_LOGD(TAG, "Read %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
           rtc_time.year, rtc_time.month, rtc_time.day_of_month,
           rtc_time.hour, rtc_time.minute, rtc_time.second,
           rtc_time.day_of_week);

  time::RealTimeClock::synchronize_epoch_(rtc_time.timestamp);
}

// ---------------------------------------------------------------------------
// write_time() - freeze counter, write 0x10-0x16, restart counter
// ---------------------------------------------------------------------------
void RX8XXXComponent::write_time() {
  auto now = time::RealTimeClock::utcnow();
  if (!now.is_valid()) {
    ESP_LOGE(TAG, "Invalid system time, not syncing to RTC");
    return;
  }

  // Freeze the timekeeping counter while writing to prevent partial-update glitches.
  if (!this->set_stop_bit_(true)) {
    ESP_LOGE(TAG, "Failed to set STOP bit before time write");
    return;
  }

  uint8_t buf[7];
  buf[0] = uint8_to_bcd_(now.second);
  buf[1] = uint8_to_bcd_(now.minute);
  buf[2] = uint8_to_bcd_(now.hour);
  buf[3] = weekday_to_onehot_(now.day_of_week);
  buf[4] = uint8_to_bcd_(now.day_of_month);
  buf[5] = uint8_to_bcd_(now.month);
  buf[6] = uint8_to_bcd_(static_cast<uint8_t>((now.year - 2000) % 100));

  bool ok = this->write_bytes(RX8XXX_REG_SEC, buf, sizeof(buf));

  // Always attempt to clear STOP, even if the write failed.
  if (!this->set_stop_bit_(false)) {
    ESP_LOGE(TAG, "Failed to clear STOP bit after time write");
  }

  if (!ok) {
    ESP_LOGE(TAG, "Can't write time registers");
    return;
  }

  ESP_LOGD(TAG, "Wrote %04d-%02d-%02d %02d:%02d:%02d DOW=%d",
           now.year, now.month, now.day_of_month,
           now.hour, now.minute, now.second,
           now.day_of_week);
}

// ---------------------------------------------------------------------------
// set_stop_bit_() - read-modify-write the STOP bit in the control register.
// Bit7 of the control register is always masked to 0 to prevent writing the
// TEST bit (bit7) on the RX8130CE, which must never be set to 1.
// ---------------------------------------------------------------------------
bool RX8XXXComponent::set_stop_bit_(bool stop) {
  uint8_t ctrl = 0;
  if (!this->read_byte(this->control_reg_addr_(), &ctrl)) {
    return false;
  }
  if (stop) {
    ctrl |= this->stop_bit_mask_();
  } else {
    ctrl &= ~this->stop_bit_mask_();
  }
  ctrl &= 0x7F;  // never set bit7 (TEST bit on RX8130CE)
  return this->write_byte(this->control_reg_addr_(), ctrl);
}

}  // namespace rx8xxx
}  // namespace esphome
