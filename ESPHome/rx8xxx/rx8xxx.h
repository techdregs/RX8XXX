#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome {
namespace rx8xxx {

// ---------------------------------------------------------------------------
// Register addresses shared by both RX8111CE and RX8130CE
// ---------------------------------------------------------------------------
static const uint8_t RX8XXX_REG_SEC        = 0x10;  ///< Seconds  BCD 00-59, bit7 unused
static const uint8_t RX8XXX_REG_MIN        = 0x11;  ///< Minutes  BCD 00-59, bit7 unused
static const uint8_t RX8XXX_REG_HOUR       = 0x12;  ///< Hours    BCD 00-23, bits7:6 unused
static const uint8_t RX8XXX_REG_WEEK       = 0x13;  ///< Weekday  one-hot: Sun=0x01...Sat=0x40
static const uint8_t RX8XXX_REG_DAY        = 0x14;  ///< Day      BCD 01-31, bits7:6 unused
static const uint8_t RX8XXX_REG_MONTH      = 0x15;  ///< Month    BCD 01-12, bits7:5 unused
static const uint8_t RX8XXX_REG_YEAR       = 0x16;  ///< Year     BCD 00-99
static const uint8_t RX8XXX_REG_MIN_ALARM  = 0x17;  ///< Minute alarm (AE bit7)
static const uint8_t RX8XXX_REG_HOUR_ALARM = 0x18;  ///< Hour alarm   (AE bit7)
static const uint8_t RX8XXX_REG_WD_ALARM   = 0x19;  ///< Week/day alarm (AE bit7, WADA selects)
static const uint8_t RX8XXX_REG_TIMER0     = 0x1A;  ///< Timer counter LSB (both chips)
static const uint8_t RX8XXX_REG_TIMER1     = 0x1B;  ///< Timer counter byte1 (both chips)

// AE bit: when set the alarm field is disabled (active-low enable logic)
static const uint8_t RX8XXX_ALARM_AE = 0x80;

// ---------------------------------------------------------------------------
// FOUT frequency selection (FSEL1:FSEL0 in extension register, same encoding
// on both chips: bits 7:6, mask 0xC0)
// ---------------------------------------------------------------------------
static const uint8_t RX8XXX_EXT_FSEL_SHIFT = 6;
static const uint8_t RX8XXX_EXT_FSEL_MASK  = 0xC0;
enum FoutFrequency : uint8_t {
  FOUT_32768_HZ = 0,  ///< 32.768 kHz (default after POR on RX8130CE)
  FOUT_1024_HZ  = 1,  ///< 1024 Hz
  FOUT_1_HZ     = 2,  ///< 1 Hz
  FOUT_OFF      = 3,  ///< Output disabled
};

// ---------------------------------------------------------------------------
// Timer source clock selection
// ---------------------------------------------------------------------------
enum TimerClock : uint8_t {
  TIMER_4096_HZ   = 0,  ///< 4096 Hz
  TIMER_64_HZ     = 1,  ///< 64 Hz
  TIMER_1_HZ      = 2,  ///< 1 Hz
  TIMER_1_60_HZ   = 3,  ///< 1/60 Hz (once per minute)
  TIMER_1_3600_HZ = 4,  ///< 1/3600 Hz (once per hour) - RX8130CE only
};

// ---------------------------------------------------------------------------
// RX8XXXComponent - abstract base class
// ---------------------------------------------------------------------------
class RX8XXXComponent : public time::RealTimeClock, public i2c::I2CDevice {
 public:
  // ---- ESPHome component lifecycle ----------------------------------------
  void setup() override;
  void update() override;
  void dump_config() override;

  // ---- Public time API ----------------------------------------------------
  void read_time();
  void write_time();

  // ---- Configuration setters (called from Python-generated code) ----------
  void set_battery_backup(bool enable) { this->battery_backup_ = enable; }
  void set_battery_charging(bool enable) { this->battery_charging_ = enable; }
  void set_fout_frequency(FoutFrequency freq) { this->fout_frequency_ = freq; }

  // Alarm configuration
  void set_alarm_second(uint8_t sec) { this->alarm_second_ = sec; }     // 0xFF = disabled
  void set_alarm_minute(uint8_t min) { this->alarm_minute_ = min; }     // 0xFF = disabled
  void set_alarm_hour(uint8_t hour) { this->alarm_hour_ = hour; }       // 0xFF = disabled
  void set_alarm_weekday(uint8_t wday) { this->alarm_weekday_ = wday; } // 0xFF = use day
  void set_alarm_day(uint8_t day) { this->alarm_day_ = day; }           // 0xFF = disabled
  void set_alarm_enabled(bool enable) { this->alarm_enabled_ = enable; }

  // Wake-up timer configuration
  void set_timer_count(uint32_t count) { this->timer_count_ = count; }
  void set_timer_clock(TimerClock clk) { this->timer_clock_ = clk; }
  void set_timer_enabled(bool enable) { this->timer_enabled_ = enable; }

  // ---- Binary sensor setters (common to both chips) -----------------------
  void set_vlf_binary_sensor(binary_sensor::BinarySensor *s) { this->vlf_sensor_ = s; }
  void set_alarm_flag_binary_sensor(binary_sensor::BinarySensor *s) { this->alarm_flag_sensor_ = s; }
  void set_timer_flag_binary_sensor(binary_sensor::BinarySensor *s) { this->timer_flag_sensor_ = s; }

  // ---- Callbacks (fire on rising-edge flag transition 0->1) ---------------
  // ESPHome's Python codegen passes a Trigger<>* for each on_alarm/on_timer
  // block; calling trigger->trigger() fires the connected automation actions.
  void add_on_alarm_callback(Trigger<> *trigger) {
    this->alarm_triggers_.push_back(trigger);
  }
  void add_on_timer_callback(Trigger<> *trigger) {
    this->timer_triggers_.push_back(trigger);
  }

  // ---- Explicit flag-clear actions (called from user automations) ----------
  void clear_alarm_flag() { this->clear_alarm_flag_(); }
  void clear_timer_flag() { this->clear_timer_flag_(); }

  // ---- Runtime alarm programming -------------------------------------------
  // Sentinel values (0xFF) mean "field disabled / not specified".
  bool set_alarm_runtime(uint8_t second, uint8_t minute, uint8_t hour,
                         uint8_t weekday, uint8_t day, int8_t enabled_override);
  bool supports_alarm_second() const { return this->supports_alarm_second_(); }
  bool is_alarm_enabled() const { return this->alarm_enabled_; }

 protected:
  // ---- Chip-specific register addresses (override in each subclass) --------
  virtual uint8_t flag_reg_addr_() = 0;
  virtual uint8_t extension_reg_addr_() = 0;
  virtual uint8_t control_reg_addr_() = 0;

  // ---- Chip-specific bit masks within those registers ---------------------
  virtual uint8_t vlf_flag_mask_() = 0;
  virtual uint8_t stop_bit_mask_() = 0;
  virtual uint8_t alarm_flag_mask_() = 0;
  virtual uint8_t timer_flag_mask_() = 0;
  virtual bool supports_alarm_second_() const { return false; }

  // ---- Chip-specific operations -------------------------------------------
  virtual bool check_and_clear_vlf_() = 0;
  virtual bool configure_battery_backup_() = 0;
  bool configure_fout_();
  virtual bool configure_alarm_() = 0;
  virtual bool configure_timer_() = 0;
  virtual bool disable_alarm_() = 0;
  virtual bool disable_timer_() = 0;
  virtual bool apply_runtime_options_() { return true; }
  bool clear_alarm_flag_();
  bool clear_timer_flag_();

  /// Called from update() after reading the flag register.
  /// Subclasses publish any chip-specific binary sensors here.
  virtual void update_chip_binary_sensors_(uint8_t flag_byte) {}

  virtual const char *model_name_() = 0;

  // ---- Shared helpers (non-virtual) ----------------------------------------
  static uint8_t bcd_to_uint8_(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
  static uint8_t uint8_to_bcd_(uint8_t val) { return ((val / 10) << 4) | (val % 10); }
  static uint8_t weekday_to_onehot_(uint8_t dow) { return static_cast<uint8_t>(1u << (dow - 1)); }
  static uint8_t onehot_to_weekday_(uint8_t onehot) {
    return static_cast<uint8_t>(__builtin_ctz(onehot & 0x7F) + 1);
  }

  /// Read-modify-write the STOP bit in the control register.
  /// Masks bit7 (TEST bit on RX8130CE) to zero on every write.
  bool set_stop_bit_(bool stop);

  // ---- Configuration state -------------------------------------------------
  bool battery_backup_{false};
  bool battery_charging_{false};
  FoutFrequency fout_frequency_{FoutFrequency::FOUT_OFF};

  bool alarm_enabled_{false};
  uint8_t alarm_second_{0xFF};
  uint8_t alarm_minute_{0xFF};
  uint8_t alarm_hour_{0xFF};
  uint8_t alarm_weekday_{0xFF};
  uint8_t alarm_day_{0xFF};

  bool timer_enabled_{false};
  uint32_t timer_count_{0};
  TimerClock timer_clock_{TimerClock::TIMER_1_HZ};

  // ---- Binary sensor sub-component pointers --------------------------------
  binary_sensor::BinarySensor *vlf_sensor_{nullptr};
  binary_sensor::BinarySensor *alarm_flag_sensor_{nullptr};
  binary_sensor::BinarySensor *timer_flag_sensor_{nullptr};

  // ---- Edge-detection state (to fire callbacks only on 0->1 transition) ----
  bool prev_alarm_flag_{false};
  bool prev_timer_flag_{false};

  // ---- Automation triggers -------------------------------------------------
  std::vector<Trigger<> *> alarm_triggers_;
  std::vector<Trigger<> *> timer_triggers_;
};

// ---------------------------------------------------------------------------
// Automation action templates — time read/write
// ---------------------------------------------------------------------------
class AlarmTrigger : public Trigger<> {};
class TimerTrigger : public Trigger<> {};

template<typename... Ts>
class SetAlarmAction : public Action<Ts...>, public Parented<RX8XXXComponent> {
 public:
  void set_enabled(bool enabled) {
    this->has_enabled_ = true;
    this->enabled_ = enabled;
  }
  void set_second(uint8_t second) {
    this->has_second_ = true;
    this->second_ = second;
  }
  void set_minute(uint8_t minute) {
    this->has_minute_ = true;
    this->minute_ = minute;
  }
  void set_hour(uint8_t hour) {
    this->has_hour_ = true;
    this->hour_ = hour;
  }
  void set_weekday(uint8_t weekday) {
    this->has_weekday_ = true;
    this->weekday_ = weekday;
  }
  void set_day(uint8_t day) {
    this->has_day_ = true;
    this->day_ = day;
  }

  void play(const Ts &...x) override {
    const uint8_t second = this->has_second_ ? this->second_ : 0xFF;
    const uint8_t minute = this->has_minute_ ? this->minute_ : 0xFF;
    const uint8_t hour = this->has_hour_ ? this->hour_ : 0xFF;
    const uint8_t weekday = this->has_weekday_ ? this->weekday_ : 0xFF;
    const uint8_t day = this->has_day_ ? this->day_ : 0xFF;
    const int8_t enabled_override = this->has_enabled_ ? (this->enabled_ ? 1 : 0) : -1;
    this->parent_->set_alarm_runtime(second, minute, hour, weekday, day, enabled_override);
  }

 protected:
  bool has_enabled_{false};
  bool enabled_{false};
  bool has_second_{false};
  uint8_t second_{0xFF};
  bool has_minute_{false};
  uint8_t minute_{0xFF};
  bool has_hour_{false};
  uint8_t hour_{0xFF};
  bool has_weekday_{false};
  uint8_t weekday_{0xFF};
  bool has_day_{false};
  uint8_t day_{0xFF};
};

template<typename... Ts>
class ScheduleAlarmInAction : public Action<Ts...>, public Parented<RX8XXXComponent> {
 public:
  void set_seconds(uint32_t seconds) { this->seconds_ = seconds; }

  void play(const Ts &...x) override {
    if (this->seconds_ == 0) {
      ESP_LOGE("rx8xxx", "schedule_alarm_in: seconds must be > 0");
      return;
    }
    auto *parent = this->parent_;
    if (!parent->is_alarm_enabled()) {
      ESP_LOGE("rx8xxx", "schedule_alarm_in: alarm is disabled "
               "(set alarm_enabled: true or enable via rx8xxx.set_alarm)");
      return;
    }
    auto now = parent->utcnow();
    if (!now.is_valid()) {
      ESP_LOGE("rx8xxx", "schedule_alarm_in: current time is invalid");
      return;
    }
    const uint32_t target_epoch = now.timestamp + this->seconds_;
    auto target = ESPTime::from_epoch_utc(target_epoch);
    if (!target.is_valid()) {
      ESP_LOGE("rx8xxx", "schedule_alarm_in: computed target time is invalid");
      return;
    }
    // Program all available fields from the target time — identical to set_alarm.
    uint8_t second = parent->supports_alarm_second()
                         ? static_cast<uint8_t>(target.second)
                         : static_cast<uint8_t>(0xFF);
    ESP_LOGI("rx8xxx",
             "schedule_alarm_in: now=%04d-%02d-%02d %02d:%02d:%02d UTC, +%lu s "
             "-> target=%04d-%02d-%02d %02d:%02d:%02d UTC",
             now.year, now.month, now.day_of_month, now.hour, now.minute, now.second,
             static_cast<unsigned long>(this->seconds_),
             target.year, target.month, target.day_of_month, target.hour, target.minute, target.second);
    parent->set_alarm_runtime(
        second,
        static_cast<uint8_t>(target.minute),
        static_cast<uint8_t>(target.hour),
        0xFF,  // weekday not used for relative alarms
        static_cast<uint8_t>(target.day_of_month),
        -1     // preserve current alarm_enabled_ policy
    );
  }

 protected:
  uint32_t seconds_{0};
};

template<typename... Ts> class WriteAction : public Action<Ts...>, public Parented<RX8XXXComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->write_time(); }
};

template<typename... Ts> class ReadAction : public Action<Ts...>, public Parented<RX8XXXComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->read_time(); }
};

// ---------------------------------------------------------------------------
// Automation action templates — explicit flag clear
// ---------------------------------------------------------------------------
template<typename... Ts>
class ClearAlarmFlagAction : public Action<Ts...>, public Parented<RX8XXXComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->clear_alarm_flag(); }
};

template<typename... Ts>
class ClearTimerFlagAction : public Action<Ts...>, public Parented<RX8XXXComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->clear_timer_flag(); }
};

}  // namespace rx8xxx
}  // namespace esphome
