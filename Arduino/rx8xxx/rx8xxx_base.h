#pragma once

// Platform-independent RX8XXX RTC library — abstract base class
// User subclasses this (via RX8111 or RX8130) and implements the four
// i2c_read/write virtual methods to connect to their platform's I2C bus.

#include "rx8xxx_defs.h"
#include "rx8xxx_log.h"

namespace rx8xxx {

class RX8xxxBase {
 public:
  virtual ~RX8xxxBase() {}

  // ---- Lifecycle -----------------------------------------------------------
  /// Initialize the RTC. Call once after I2C bus is ready.
  /// Returns false on I2C failure.
  bool begin();

  // ---- Time operations -----------------------------------------------------
  /// Read current time from the RTC registers.
  bool read_time(RX8xxxTime *out);

  /// Write a time to the RTC registers (freezes counter, writes, restarts).
  bool write_time(const RX8xxxTime &time);

  // ---- Flag polling --------------------------------------------------------
  /// Read flag register and return current states + edge detection.
  /// Call periodically (e.g. once per second) to detect alarm/timer events.
  bool poll_flags(RX8xxxFlags *out);

  // ---- Configuration setters (call before begin()) ------------------------
  void set_battery_backup(bool enable) { this->battery_backup_ = enable; }
  void set_battery_charging(bool enable) { this->battery_charging_ = enable; }
  void set_fout_frequency(FoutFrequency freq) { this->fout_frequency_ = freq; }

  // Alarm configuration
  void set_alarm_second(uint8_t sec) { this->alarm_second_ = sec; }
  void set_alarm_minute(uint8_t min) { this->alarm_minute_ = min; }
  void set_alarm_hour(uint8_t hour) { this->alarm_hour_ = hour; }
  void set_alarm_weekday(uint8_t wday) { this->alarm_weekday_ = wday; }
  void set_alarm_day(uint8_t day) { this->alarm_day_ = day; }
  void set_alarm_enabled(bool enable) { this->alarm_enabled_ = enable; }

  // Wake-up timer configuration
  void set_timer_count(uint32_t count) { this->timer_count_ = count; }
  void set_timer_clock(TimerClock clk) { this->timer_clock_ = clk; }
  void set_timer_enabled(bool enable) { this->timer_enabled_ = enable; }

  // ---- Explicit flag-clear actions -----------------------------------------
  bool clear_alarm_flag();
  bool clear_timer_flag();

  // ---- Runtime alarm programming -------------------------------------------
  bool set_alarm_runtime(uint8_t second, uint8_t minute, uint8_t hour,
                         uint8_t weekday, uint8_t day, int8_t enabled_override);
  bool supports_alarm_second() const { return this->supports_alarm_second_(); }
  bool is_alarm_enabled() const { return this->alarm_enabled_; }

  /// Returns chip model name (e.g. "RX8111CE").
  virtual const char *model_name() const = 0;

 protected:
  // ---- I2C abstraction (user must implement) -------------------------------
  virtual bool i2c_read_byte(uint8_t reg, uint8_t *value) = 0;
  virtual bool i2c_write_byte(uint8_t reg, uint8_t value) = 0;
  virtual bool i2c_read_bytes(uint8_t reg, uint8_t *data, uint8_t len) = 0;
  virtual bool i2c_write_bytes(uint8_t reg, const uint8_t *data, uint8_t len) = 0;

  // ---- Chip-specific register addresses (override in each subclass) --------
  virtual uint8_t flag_reg_addr() const = 0;
  virtual uint8_t extension_reg_addr() const = 0;
  virtual uint8_t control_reg_addr() const = 0;

  // ---- Chip-specific bit masks --------------------------------------------
  virtual uint8_t vlf_flag_mask() const = 0;
  virtual uint8_t stop_bit_mask() const = 0;
  virtual uint8_t alarm_flag_mask() const = 0;
  virtual uint8_t timer_flag_mask() const = 0;
  virtual bool supports_alarm_second_() const { return false; }

  // ---- Chip-specific operations -------------------------------------------
  virtual bool check_and_clear_vlf() = 0;
  virtual bool configure_battery_backup() = 0;
  bool configure_fout();
  virtual bool configure_alarm() = 0;
  virtual bool configure_timer() = 0;
  virtual bool disable_alarm() = 0;
  virtual bool disable_timer() = 0;
  virtual bool apply_runtime_options() { return true; }

  /// Called from poll_flags() after reading the flag register.
  /// Subclasses can read chip-specific status here.
  virtual void read_chip_status(uint8_t /*flag_byte*/) {}

  // ---- Shared helpers (non-virtual) ----------------------------------------
  static uint8_t bcd_to_uint8(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
  static uint8_t uint8_to_bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }
  static uint8_t weekday_to_onehot(uint8_t dow) { return static_cast<uint8_t>(1u << (dow - 1)); }
  static uint8_t onehot_to_weekday(uint8_t onehot) {
    return static_cast<uint8_t>(__builtin_ctz(onehot & 0x7F) + 1);
  }

  /// Read-modify-write the STOP bit in the control register.
  bool set_stop_bit(bool stop);

  // ---- Configuration state -------------------------------------------------
  bool battery_backup_{false};
  bool battery_charging_{false};
  FoutFrequency fout_frequency_{FOUT_OFF};

  bool alarm_enabled_{false};
  uint8_t alarm_second_{0xFF};
  uint8_t alarm_minute_{0xFF};
  uint8_t alarm_hour_{0xFF};
  uint8_t alarm_weekday_{0xFF};
  uint8_t alarm_day_{0xFF};

  bool timer_enabled_{false};
  uint32_t timer_count_{0};
  TimerClock timer_clock_{TIMER_1_HZ};

  // ---- Edge-detection state ------------------------------------------------
  bool prev_alarm_flag_{false};
  bool prev_timer_flag_{false};
};

}  // namespace rx8xxx
