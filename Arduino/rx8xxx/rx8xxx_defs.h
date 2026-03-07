#pragma once

// Platform-independent RX8XXX RTC library — shared definitions
// Supports RX8111CE and RX8130CE real-time clock ICs from Epson.
// No framework dependencies. Only requires <stdint.h>.

#include <stdint.h>

namespace rx8xxx {

// ---------------------------------------------------------------------------
// Register addresses shared by both RX8111CE and RX8130CE
// ---------------------------------------------------------------------------
static const uint8_t REG_SEC        = 0x10;  ///< Seconds  BCD 00-59, bit7 unused
static const uint8_t REG_MIN        = 0x11;  ///< Minutes  BCD 00-59, bit7 unused
static const uint8_t REG_HOUR       = 0x12;  ///< Hours    BCD 00-23, bits7:6 unused
static const uint8_t REG_WEEK       = 0x13;  ///< Weekday  one-hot: Sun=0x01...Sat=0x40
static const uint8_t REG_DAY        = 0x14;  ///< Day      BCD 01-31, bits7:6 unused
static const uint8_t REG_MONTH      = 0x15;  ///< Month    BCD 01-12, bits7:5 unused
static const uint8_t REG_YEAR       = 0x16;  ///< Year     BCD 00-99
static const uint8_t REG_MIN_ALARM  = 0x17;  ///< Minute alarm (AE bit7)
static const uint8_t REG_HOUR_ALARM = 0x18;  ///< Hour alarm   (AE bit7)
static const uint8_t REG_WD_ALARM   = 0x19;  ///< Week/day alarm (AE bit7, WADA selects)
static const uint8_t REG_TIMER0     = 0x1A;  ///< Timer counter LSB (both chips)
static const uint8_t REG_TIMER1     = 0x1B;  ///< Timer counter byte1 (both chips)

// AE bit: when set the alarm field is disabled (active-low enable logic)
static const uint8_t ALARM_AE = 0x80;

// ---------------------------------------------------------------------------
// FOUT frequency selection (FSEL1:FSEL0 in extension register, same encoding
// on both chips: bits 7:6, mask 0xC0)
// ---------------------------------------------------------------------------
static const uint8_t EXT_FSEL_SHIFT = 6;
static const uint8_t EXT_FSEL_MASK  = 0xC0;

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
// Time representation — plain struct, no epoch, no dependencies
// ---------------------------------------------------------------------------
struct RX8xxxTime {
  uint16_t year;        ///< 2000-2099
  uint8_t  month;       ///< 1-12
  uint8_t  day;         ///< 1-31
  uint8_t  day_of_week; ///< 1=Sunday .. 7=Saturday
  uint8_t  hour;        ///< 0-23
  uint8_t  minute;      ///< 0-59
  uint8_t  second;      ///< 0-59
};

// ---------------------------------------------------------------------------
// Flag status returned by poll_flags()
// ---------------------------------------------------------------------------
struct RX8xxxFlags {
  bool vlf;         ///< Voltage Low Flag (oscillation lost)
  bool alarm_flag;  ///< AF is currently set
  bool timer_flag;  ///< TF is currently set
  bool alarm_edge;  ///< AF transitioned 0->1 since last poll
  bool timer_edge;  ///< TF transitioned 0->1 since last poll
};

}  // namespace rx8xxx
