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
// EVIN level selection (EHL bit in the EVIN Setting register, RX8111CE only)
// ---------------------------------------------------------------------------
enum EventLevel : uint8_t {
  EVENT_LEVEL_LOW  = 0,  ///< Trigger when EVIN is low for at least the filter period
  EVENT_LEVEL_HIGH = 1,  ///< Trigger when EVIN is high for at least the filter period
};

// ---------------------------------------------------------------------------
// RX8111 timestamp record storage mode
// ---------------------------------------------------------------------------
enum TimestampRecordMode : uint8_t {
  TIMESTAMP_RECORD_LATEST         = 0,  ///< Keep only the latest event time in 0x20-0x29
  TIMESTAMP_RECORD_STOP_WHEN_FULL = 1,  ///< Store up to 8 records in RAM then stop
  TIMESTAMP_RECORD_OVERWRITE      = 2,  ///< Store up to 8 records and wrap when full
};

// ---------------------------------------------------------------------------
// EVIN pull-up/pull-down selection (PDN, PU1, PU0 in EVIN Setting register)
// ---------------------------------------------------------------------------
enum EvinPull : uint8_t {
  EVIN_PULL_NONE        = 0,  ///< Hi-Z (no connection)
  EVIN_PULL_UP_500K     = 1,  ///< Pull-up 500 kOhm (to VOUT)
  EVIN_PULL_UP_1M       = 2,  ///< Pull-up 1 MOhm (to VOUT)
  EVIN_PULL_UP_10M      = 3,  ///< Pull-up 10 MOhm (to VOUT)
  EVIN_PULL_DOWN_500K   = 4,  ///< Pull-down 500 kOhm (to GND)
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
  bool event_flag;  ///< EVF is currently set (RX8111CE only)
  bool alarm_edge;  ///< AF transitioned 0->1 since last poll
  bool timer_edge;  ///< TF transitioned 0->1 since last poll
  bool event_edge;  ///< EVF transitioned 0->1 since last poll (RX8111CE only)
};

// ---------------------------------------------------------------------------
// RX8111 decoded event timestamp record
// ---------------------------------------------------------------------------
struct RX8111TimestampRecord {
  uint16_t year;        ///< 2000-2099
  uint8_t  month;       ///< 1-12
  uint8_t  day;         ///< 1-31
  uint8_t  hour;        ///< 0-23
  uint8_t  minute;      ///< 0-59
  uint8_t  second;      ///< 0-59
  uint16_t milliseconds;///< 0-999
  bool vlow;            ///< VBAT low at the time of the event
  bool vcmp;            ///< VDD < VBAT at the time of the event
  bool vdet;            ///< RTC was in backup mode at the time of the event
  bool xst;             ///< Crystal stop was latched at the time of the event
};

}  // namespace rx8xxx
