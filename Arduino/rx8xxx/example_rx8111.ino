// =============================================================================
// RX8XXX Library — Arduino Example for RX8111CE
// =============================================================================
//
// Demonstrates all major library features using an RX8111CE RTC connected
// via I2C. The RX8130CE works identically — just swap the class (see the
// "RX8130CE Differences" section at the bottom of this file).
//
// Wiring (same for any Arduino-compatible board):
//   RX8111CE SDA  ->  Board SDA (with 4.7k pull-up to VDD)
//   RX8111CE SCL  ->  Board SCL (with 4.7k pull-up to VDD)
//   RX8111CE VDD  ->  3.3V
//   RX8111CE VSS  ->  GND
//   RX8111CE /INT ->  (optional) connect to a GPIO for hardware interrupt
//
// I2C address: 0x32 (fixed in hardware, not configurable)
//
// =============================================================================

#include <Wire.h>
#include <stdio.h>
#include <stdarg.h>

// Include only the header for the chip you are using.
// For RX8130CE, use "rx8130.h" instead.
#include "rx8111.h"

// =============================================================================
// I2C Adapter — Bridges the library's virtual I2C methods to Arduino Wire
// =============================================================================
// You must subclass your chip (RX8111 or RX8130) and implement the four
// i2c_read/write methods. This is the only platform-specific code required.

static const uint8_t RTC_I2C_ADDR = 0x32;

class ArduinoRX8111 : public rx8xxx::RX8111 {
 protected:
  bool i2c_read_byte(uint8_t reg, uint8_t *value) override {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(RTC_I2C_ADDR, (uint8_t)1) != 1) return false;
    *value = Wire.read();
    return true;
  }

  bool i2c_write_byte(uint8_t reg, uint8_t value) override {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

  bool i2c_read_bytes(uint8_t reg, uint8_t *data, uint8_t len) override {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(RTC_I2C_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) {
      data[i] = Wire.read();
    }
    return true;
  }

  bool i2c_write_bytes(uint8_t reg, const uint8_t *data, uint8_t len) override {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) {
      Wire.write(data[i]);
    }
    return Wire.endTransmission() == 0;
  }
};

// =============================================================================
// Global RTC instance
// =============================================================================
ArduinoRX8111 rtc;

// =============================================================================
// Optional: Log handler — routes library debug output to Serial
// =============================================================================
// The library is completely silent by default. Assign this function to
// rx8xxx::rx8xxx_log_func to see internal warnings and debug messages.
// Remove or leave unset in production to save flash and CPU cycles.

void serial_log(rx8xxx::RX8xxxLogLevel level, const char *tag, const char *fmt, ...) {
  // Print severity prefix
  const char *prefix = "???";
  switch (level) {
    case rx8xxx::RX8_LOG_ERROR: prefix = "ERR"; break;
    case rx8xxx::RX8_LOG_WARN:  prefix = "WRN"; break;
    case rx8xxx::RX8_LOG_INFO:  prefix = "INF"; break;
    case rx8xxx::RX8_LOG_DEBUG: prefix = "DBG"; break;
  }

  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.print("[");
  Serial.print(prefix);
  Serial.print("] [");
  Serial.print(tag);
  Serial.print("] ");
  Serial.println(buf);
}

// =============================================================================
// Helper: print an RX8xxxTime to Serial
// =============================================================================
void print_time(const char *label, const rx8xxx::RX8xxxTime &t) {
  char buf[40];
  // Day-of-week names (1=Sunday .. 7=Saturday)
  static const char *dow_names[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d (%s)",
           t.year, t.month, t.day, t.hour, t.minute, t.second,
           (t.day_of_week >= 1 && t.day_of_week <= 7) ? dow_names[t.day_of_week] : "?");
  Serial.print(label);
  Serial.println(buf);
}

// =============================================================================
// setup()
// =============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }  // Wait for serial on USB-native boards (Leonardo, etc.)
  Serial.println();
  Serial.println("=== RX8XXX Library Demo (RX8111CE) ===");
  Serial.println();

  Wire.begin();

  // ------------------------------------------------------------------
  // 1. OPTIONAL: Enable logging
  // ------------------------------------------------------------------
  // Assign the log callback to see library debug output on Serial.
  // Comment this line out to disable all library logging.
  rx8xxx::rx8xxx_log_func = serial_log;

  // ------------------------------------------------------------------
  // 2. Configure features BEFORE calling begin()
  // ------------------------------------------------------------------
  // All configuration setters store values internally; begin() writes
  // them to the RTC hardware in the correct order.

  // --- Battery backup ---
  // Enables automatic switchover to backup battery when VDD drops.
  // set_battery_charging(true) additionally enables trickle charging
  // for a rechargeable backup cell (e.g. supercapacitor or ML battery).
  rtc.set_battery_backup(true);
  rtc.set_battery_charging(false);  // Set true only with a rechargeable backup cell

  // --- FOUT clock output ---
  // The FOUT pin can output a square wave for clocking external circuits.
  //   FOUT_32768_HZ — 32.768 kHz (useful for driving another IC's clock input)
  //   FOUT_1024_HZ  — 1024 Hz
  //   FOUT_1_HZ     — 1 Hz (handy for a heartbeat LED or watchdog)
  //   FOUT_OFF      — disabled (default, saves power)
  rtc.set_fout_frequency(rx8xxx::FOUT_OFF);

  // --- Alarm ---
  // The alarm fires when the current time matches ALL enabled alarm fields.
  // Each field is independently enabled; set to 0xFF to disable (don't-care).
  // The alarm drives /INT low and stays latched until you call clear_alarm_flag().
  //
  // Example: alarm at 07:30:00 every day
  rtc.set_alarm_enabled(true);
  rtc.set_alarm_second(0);    // Match on second 0 (RX8111CE only — see note below)
  rtc.set_alarm_minute(30);   // Match on minute 30
  rtc.set_alarm_hour(7);      // Match on hour 7
  rtc.set_alarm_weekday(0xFF);  // 0xFF = don't care (match any weekday)
  rtc.set_alarm_day(0xFF);      // 0xFF = don't care (match any day-of-month)

  //
  // ALARM FIELD NOTES:
  //
  //   set_alarm_second():
  //     Only supported on RX8111CE. The RX8130CE does not have a second alarm
  //     register; the library ignores it silently for that chip.
  //     Query rtc.supports_alarm_second() at runtime to check.
  //
  //   set_alarm_weekday() vs set_alarm_day():
  //     These are mutually exclusive — set one, leave the other 0xFF.
  //     - Weekday uses 1=Sunday .. 7=Saturday (same as day_of_week in RX8xxxTime).
  //     - Day uses 1-31 (day of month, BCD encoded internally).
  //     Setting both is an error (set_alarm_runtime will return false).
  //
  //   WADA bit:
  //     The library automatically selects WADA=0 (weekday mode) or WADA=1
  //     (day-of-month mode) based on which field you set. You never touch
  //     WADA directly.
  //

  // --- Wake-up timer ---
  // A countdown timer that fires periodically and drives /INT low briefly.
  // Unlike the alarm, the timer /INT auto-releases after ~7.8ms (tRTN);
  // TF stays set until you call clear_timer_flag().
  //
  // Timer count + clock source determine the period:
  //   period = count / clock_frequency
  //
  // Example: fire every 10 seconds (count=10, clock=1Hz)
  rtc.set_timer_enabled(true);
  rtc.set_timer_count(10);
  rtc.set_timer_clock(rx8xxx::TIMER_1_HZ);

  //
  // TIMER CLOCK OPTIONS:
  //   TIMER_4096_HZ   — 4096 Hz (sub-ms precision, max ~4096s at 24-bit)
  //   TIMER_64_HZ     — 64 Hz   (15.6ms resolution)
  //   TIMER_1_HZ      — 1 Hz    (1s resolution, most common)
  //   TIMER_1_60_HZ   — 1/60 Hz (1-minute resolution)
  //   TIMER_1_3600_HZ — 1/3600 Hz (1-hour resolution, RX8130CE only)
  //
  // TIMER COUNT LIMITS:
  //   RX8111CE: 24-bit counter, max 16,777,215
  //   RX8130CE: 16-bit counter, max 65,535 (values above are clamped)
  //

  // --- Timestamp engine (RX8111CE only) ---
  // When enabled, the chip records timestamps of external events detected
  // on the EVIN pin into internal RAM (0x40-0x7F).
  rtc.set_timestamp_enabled(false);

  // ------------------------------------------------------------------
  // 3. Initialize the RTC
  // ------------------------------------------------------------------
  // begin() performs:
  //   - Reads and clears startup flags (VLF, XST, POR)
  //   - Configures battery backup
  //   - Configures FOUT
  //   - Programs alarm registers (or disables alarm if not enabled)
  //   - Programs timer registers (or disables timer if not enabled)
  //   - Applies chip-specific options (timestamp engine, etc.)
  //
  // Returns false on I2C failure. VLF/XST warnings are logged but don't
  // cause begin() to fail — the chip is still functional, but time data
  // may be invalid and should be rewritten.

  if (!rtc.begin()) {
    Serial.println("ERROR: RTC initialization failed (I2C error).");
    Serial.println("Check wiring: SDA, SCL, VDD, GND.");
    while (1) { delay(1000); }
  }

  Serial.print("RTC initialized: ");
  Serial.println(rtc.model_name());

  // ------------------------------------------------------------------
  // 4. Read the current time
  // ------------------------------------------------------------------
  rx8xxx::RX8xxxTime now;
  if (rtc.read_time(&now)) {
    print_time("Current RTC time: ", now);
  } else {
    Serial.println("ERROR: Failed to read time from RTC.");
  }

  // ------------------------------------------------------------------
  // 5. Write a new time (use to set the clock initially)
  // ------------------------------------------------------------------
  // Uncomment the block below to set the RTC to a specific time.
  // After setting, comment it out again to avoid resetting on every boot.
  /*
  {
    rx8xxx::RX8xxxTime set_time;
    set_time.year        = 2026;
    set_time.month       = 2;
    set_time.day         = 25;
    set_time.day_of_week = 4;  // 1=Sun, 2=Mon, 3=Tue, 4=Wed, 5=Thu, 6=Fri, 7=Sat
    set_time.hour        = 14;
    set_time.minute      = 30;
    set_time.second      = 0;

    if (rtc.write_time(set_time)) {
      Serial.println("RTC time set successfully.");
    } else {
      Serial.println("ERROR: Failed to write time to RTC.");
    }
  }
  */

  // ------------------------------------------------------------------
  // 6. Check chip-specific status (RX8111CE)
  // ------------------------------------------------------------------
  // These accessors are populated after poll_flags() or after begin().
  // For RX8111CE:
  //   xst_flag()    — true if crystal oscillation stop was detected
  //   battery_low() — true if VBAT is below the low threshold
  //   evin_state()  — current logic level of the EVIN pin

  // Do an initial poll to populate chip status.
  rx8xxx::RX8xxxFlags flags;
  if (rtc.poll_flags(&flags)) {
    Serial.print("VLF (oscillation lost): ");
    Serial.println(flags.vlf ? "YES" : "no");
    Serial.print("Crystal stop (XST):    ");
    Serial.println(rtc.xst_flag() ? "YES" : "no");
    Serial.print("Battery low (VLOW):    ");
    Serial.println(rtc.battery_low() ? "YES" : "no");
    Serial.print("EVIN pin state:        ");
    Serial.println(rtc.evin_state() ? "HIGH" : "LOW");
  }

  Serial.println();
  Serial.println("Entering main loop — polling flags every second...");
  Serial.println("(Alarm set for 07:30:00, Timer fires every 10s)");
  Serial.println();
}

// =============================================================================
// loop() — periodic polling
// =============================================================================
// The library uses a polling model: call poll_flags() periodically and check
// the returned RX8xxxFlags struct. The alarm_edge and timer_edge booleans are
// true only on the first poll after a 0->1 flag transition, so your handler
// fires exactly once per event regardless of poll frequency.

unsigned long last_poll_ms = 0;
unsigned long last_time_print_ms = 0;

void loop() {
  unsigned long now_ms = millis();

  // ------------------------------------------------------------------
  // Poll flags once per second
  // ------------------------------------------------------------------
  if (now_ms - last_poll_ms >= 1000) {
    last_poll_ms = now_ms;

    rx8xxx::RX8xxxFlags flags;
    if (rtc.poll_flags(&flags)) {

      // --- Alarm event (rising edge of AF) ---
      // alarm_edge is true exactly once when AF transitions from 0 to 1.
      // After handling, you MUST call clear_alarm_flag() to:
      //   1. Release the /INT pin (it stays low until cleared)
      //   2. Allow the next alarm event to be detected
      if (flags.alarm_edge) {
        Serial.println(">>> ALARM FIRED! <<<");
        rtc.clear_alarm_flag();
      }

      // --- Timer event (rising edge of TF) ---
      // timer_edge is true exactly once when TF transitions from 0 to 1.
      // clear_timer_flag() re-arms edge detection for the next event.
      // Note: the timer /INT pin auto-releases after ~7.8ms regardless.
      if (flags.timer_edge) {
        Serial.println(">>> TIMER FIRED! <<<");
        rtc.clear_timer_flag();
      }

      // --- VLF warning ---
      // If VLF is set, the oscillator was interrupted. Time may be wrong.
      if (flags.vlf) {
        Serial.println("WARNING: VLF is set — time data may be invalid.");
      }
    }
  }

  // ------------------------------------------------------------------
  // Print current time every 10 seconds
  // ------------------------------------------------------------------
  if (now_ms - last_time_print_ms >= 10000) {
    last_time_print_ms = now_ms;

    rx8xxx::RX8xxxTime t;
    if (rtc.read_time(&t)) {
      print_time("Time: ", t);
    }
  }

  // ------------------------------------------------------------------
  // Runtime alarm reprogramming (example: change alarm via Serial)
  // ------------------------------------------------------------------
  // The alarm can be reprogrammed at any time without calling begin() again.
  // set_alarm_runtime() writes directly to the hardware registers.
  //
  // Send 'A' over Serial to reprogram the alarm to 12:00:00 as a demo.
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'A' || c == 'a') {
      Serial.println("Reprogramming alarm to 12:00:00 daily...");

      // set_alarm_runtime(second, minute, hour, weekday, day, enabled_override)
      //   - Use 0xFF for any field you want to disable (don't-care).
      //   - enabled_override: 1=enable, 0=disable, -1=keep current setting.
      bool ok = rtc.set_alarm_runtime(
          0,      // second = 0 (RX8111CE only; ignored on RX8130CE)
          0,      // minute = 0
          12,     // hour   = 12
          0xFF,   // weekday = don't care
          0xFF,   // day     = don't care
          -1      // keep current enabled state
      );

      if (ok) {
        Serial.println("Alarm reprogrammed successfully.");
      } else {
        Serial.println("ERROR: Alarm reprogramming failed.");
      }
    }

    // Send 'D' to disable the alarm entirely.
    if (c == 'D' || c == 'd') {
      Serial.println("Disabling alarm...");
      rtc.set_alarm_runtime(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0);  // enabled_override=0
      Serial.print("Alarm enabled: ");
      Serial.println(rtc.is_alarm_enabled() ? "yes" : "no");
    }

    // Send 'E' to re-enable the alarm with current settings.
    if (c == 'E' || c == 'e') {
      Serial.println("Re-enabling alarm...");
      rtc.set_alarm_runtime(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1);  // enabled_override=1
      Serial.print("Alarm enabled: ");
      Serial.println(rtc.is_alarm_enabled() ? "yes" : "no");
    }

    // Send 'S' to query supports_alarm_second().
    if (c == 'S' || c == 's') {
      Serial.print("Supports per-second alarm: ");
      Serial.println(rtc.supports_alarm_second() ? "yes" : "no");
    }
  }
}

// =============================================================================
// RX8130CE Differences
// =============================================================================
//
// To use an RX8130CE instead, make these changes:
//
// 1. Include the RX8130 header instead:
//      #include "rx8130.h"
//
// 2. Subclass rx8xxx::RX8130 instead of rx8xxx::RX8111:
//      class ArduinoRX8130 : public rx8xxx::RX8130 { ... };
//      (the I2C implementation is identical)
//
// 3. Remove RX8111-only features:
//      - set_alarm_second() — RX8130CE has no second alarm register.
//        supports_alarm_second() returns false on RX8130CE.
//      - set_timestamp_enabled() — RX8130CE has no timestamp engine.
//      - xst_flag(), battery_low(), evin_state() — not available.
//
// 4. Use RX8130-specific features instead:
//      - set_digital_offset(int8_t offset):
//        Apply a digital frequency correction to the 32.768 kHz oscillator.
//        Each LSB is approximately 3.05 ppm. Range: -64 to +63.
//        Example: rtc.set_digital_offset(5);  // +15.25 ppm correction
//        Call before begin(); the value is written during initialization.
//
//      - vbff_flag():
//        Returns true if the backup battery (VBAT) has failed (critically low).
//        Valid after poll_flags().
//
//      - vblf_flag():
//        Returns true if the main VDD supply battery level is low.
//        Valid after poll_flags().
//
// 5. Timer differences:
//      - RX8130CE timer is 16-bit (max 65,535). Values above are clamped.
//      - RX8130CE supports TIMER_1_3600_HZ (1 event per hour).
//        RX8111CE does not — it clamps to TIMER_1_60_HZ.
//
// Minimal RX8130CE example:
//
//   #include "rx8130.h"
//
//   class ArduinoRX8130 : public rx8xxx::RX8130 {
//    protected:
//     // ... same four i2c methods as ArduinoRX8111 above ...
//   };
//
//   ArduinoRX8130 rtc;
//
//   void setup() {
//     Wire.begin();
//     rtc.set_battery_backup(true);
//     rtc.set_digital_offset(0);     // no frequency correction
//     rtc.set_alarm_enabled(true);
//     rtc.set_alarm_minute(30);
//     rtc.set_alarm_hour(7);
//     rtc.begin();
//   }
//
//   void loop() {
//     rx8xxx::RX8xxxFlags flags;
//     if (rtc.poll_flags(&flags)) {
//       if (flags.alarm_edge) {
//         // handle alarm
//         rtc.clear_alarm_flag();
//       }
//       if (rtc.vbff_flag()) {
//         // backup battery is dead
//       }
//       if (rtc.vblf_flag()) {
//         // main battery is low
//       }
//     }
//     delay(1000);
//   }
