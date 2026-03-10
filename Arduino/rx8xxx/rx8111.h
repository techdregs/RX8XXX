#pragma once

// Platform-independent RX8XXX RTC library — RX8111CE implementation

#include "rx8xxx_base.h"

namespace rx8xxx {

// ---------------------------------------------------------------------------
// RX8111CE register addresses
// ---------------------------------------------------------------------------
static const uint8_t RX8111_REG_EXTENSION  = 0x1D;  ///< FSEL1,FSEL0,USEL,TE,WADA,ETS,TSEL1,TSEL0
static const uint8_t RX8111_REG_FLAG       = 0x1E;  ///< POR,z,UF,TF,AF,EVF,VLF,XST
static const uint8_t RX8111_REG_CONTROL    = 0x1F;  ///< z,z,UIE,TIE,AIE,EIE,z,STOP
static const uint8_t RX8111_REG_SEC_ALARM  = 0x2C;  ///< Second alarm (AE bit7) - unique to RX8111
static const uint8_t RX8111_REG_TIMER2     = 0x1C;  ///< Timer counter MSB (24-bit timer)
static const uint8_t RX8111_REG_TIMERCTRL  = 0x2D;  ///< TBKON,TBKE,TMPIN,TSTP
static const uint8_t RX8111_REG_PWRSWITCH  = 0x32;  ///< CHGEN(7),INIEN(6),SWSEL1(3),SWSEL0(2),SMPT1(1),SMPT0(0)
static const uint8_t RX8111_REG_STATUS     = 0x33;  ///< EVIN(6),VCMP(2),VLOW(1)
static const uint8_t RX8111_REG_TSCTRL1    = 0x34;  ///< EISEL(2),TSCLR(1),TSRAM(0)
static const uint8_t RX8111_REG_TSCTRL2    = 0x35;  ///< ECMP(3),EVDET(2),EVLOW(1),EXST(0)
static const uint8_t RX8111_REG_TSCTRL3    = 0x36;  ///< TSFULL(4),TSEMP(3),TSAD2(2),TSAD1(1),TSAD0(0)
static const uint8_t RX8111_REG_TIMESTAMP_SINGLE = 0x20;  ///< Latest event timestamp block (0x20-0x29)
static const uint8_t RX8111_REG_TIMESTAMP_MULTI  = 0x40;  ///< Timestamp RAM base (0x40-0x7F)

// ---------------------------------------------------------------------------
// RX8111CE flag register (0x1E) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_FLAG_POR  = 0x80;  ///< Power-On Reset flag
static const uint8_t RX8111_FLAG_UF   = 0x20;  ///< Time Update Flag
static const uint8_t RX8111_FLAG_TF   = 0x10;  ///< Timer Flag
static const uint8_t RX8111_FLAG_AF   = 0x08;  ///< Alarm Flag
static const uint8_t RX8111_FLAG_EVF  = 0x04;  ///< Event (time stamp) Flag
static const uint8_t RX8111_FLAG_VLF  = 0x02;  ///< Voltage Low Flag (oscillation lost)
static const uint8_t RX8111_FLAG_XST  = 0x01;  ///< Crystal Oscillation Stop flag

// ---------------------------------------------------------------------------
// RX8111CE control register (0x1F) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_CTRL_UIE  = 0x20;  ///< Update Interrupt Enable
static const uint8_t RX8111_CTRL_TIE  = 0x10;  ///< Timer Interrupt Enable
static const uint8_t RX8111_CTRL_AIE  = 0x08;  ///< Alarm Interrupt Enable
static const uint8_t RX8111_CTRL_EIE  = 0x04;  ///< Event Interrupt Enable
static const uint8_t RX8111_CTRL_STOP = 0x01;  ///< Stop bit - halts timekeeping

// ---------------------------------------------------------------------------
// RX8111CE extension register (0x1D) bit masks / shifts
// ---------------------------------------------------------------------------
static const uint8_t RX8111_EXT_TE         = 0x10;  ///< Timer Enable
static const uint8_t RX8111_EXT_WADA       = 0x08;  ///< Week/Day Alarm select (1=DAY, 0=WEEK)
static const uint8_t RX8111_EXT_ETS        = 0x04;  ///< Event Time Stamp enable
static const uint8_t RX8111_EXT_TSEL_MASK  = 0x03;  ///< TSEL1:TSEL0 in bits 1:0

// ---------------------------------------------------------------------------
// RX8111CE EVIN Setting register (0x2B) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_REG_EVIN_SETTING = 0x2B;  ///< EHL(7),ET1(6),ET0(5),PDN(4),PU1(3),PU0(2),OVW(1),-(0)
static const uint8_t RX8111_EVIN_EHL       = 0x80;  ///< EVIN High/Low level select (1=high, 0=low)
static const uint8_t RX8111_EVIN_ET_MASK   = 0x60;  ///< ET1:ET0 chattering filter mask
static const uint8_t RX8111_EVIN_ET_SHIFT  = 5;     ///< ET1:ET0 bit shift
static const uint8_t RX8111_EVIN_PDN       = 0x10;  ///< Pull-down select
static const uint8_t RX8111_EVIN_PU1       = 0x08;  ///< Pull-up bit 1
static const uint8_t RX8111_EVIN_PU0       = 0x04;  ///< Pull-up bit 0
static const uint8_t RX8111_EVIN_OVW       = 0x02;  ///< Timestamp overwrite mode

// ---------------------------------------------------------------------------
// RX8111CE timestamp control register 1 (0x34) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_TSCTRL1_EISEL  = 0x04;  ///< Interrupt every event vs after 8 records
static const uint8_t RX8111_TSCTRL1_TSCLR  = 0x02;  ///< Clear timestamp RAM pointer
static const uint8_t RX8111_TSCTRL1_TSRAM  = 0x01;  ///< Route 0x40-0x7F RAM to timestamp storage

// ---------------------------------------------------------------------------
// RX8111CE power switch register (0x32) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_PWR_CHGEN  = 0x80;  ///< Charge Enable
static const uint8_t RX8111_PWR_INIEN  = 0x40;  ///< Power Switch (interface) Enable

// ---------------------------------------------------------------------------
// RX8111CE status monitor register (0x33) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_STATUS_EVIN = 0x40;  ///< Current EVIN pin voltage level
static const uint8_t RX8111_STATUS_VCMP = 0x04;  ///< VDD comparison result
static const uint8_t RX8111_STATUS_VLOW = 0x02;  ///< VBAT low detection flag

// ---------------------------------------------------------------------------
// RX8111CE timestamp control register 3 (0x36) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_TSCTRL3_TSFULL = 0x10;  ///< Timestamp RAM full
static const uint8_t RX8111_TSCTRL3_TSEMP  = 0x08;  ///< Timestamp RAM empty
static const uint8_t RX8111_TSCTRL3_TSAD_MASK = 0x07;  ///< Latest timestamp RAM slot pointer

// ---------------------------------------------------------------------------
// RX8111 — concrete implementation for RX8111CE
// ---------------------------------------------------------------------------
class RX8111 : public RX8xxxBase {
 public:
  /// Enable hardware event timestamp capture independently of EVF interrupting.
  /// The library remains polling-based: EVF is observed via poll_flags(), and
  /// timestamp records are read only when the caller invokes the timestamp APIs.
  void set_timestamp_enabled(bool enable) { this->timestamp_enabled_ = enable; }
  void set_timestamp_record_mode(TimestampRecordMode mode) { this->timestamp_record_mode_ = mode; }
  void set_event_level(EventLevel level) { this->event_level_ = level; }
  void set_event_enabled(bool enable) { this->event_enabled_ = enable; }
  /// Chattering filter selection: 0..3, matching the hardware ET1:ET0 levels.
  /// Values above 3 are clamped.
  void set_evin_filter(uint8_t level) { this->evin_filter_ = level > 3 ? 3 : level; }
  void set_evin_pull(EvinPull pull) { this->evin_pull_ = pull; }

  /// Reads the latest timestamp record from 0x20-0x29.
  /// Valid only when timestamp_record_mode is TIMESTAMP_RECORD_LATEST.
  bool read_latest_event_timestamp(RX8111TimestampRecord *out);

  /// Reads buffered timestamp RAM oldest-to-newest and clears it only after a
  /// full successful drain. This does not clear EVF.
  /// Valid only when timestamp_record_mode is TIMESTAMP_RECORD_STOP_WHEN_FULL
  /// or TIMESTAMP_RECORD_OVERWRITE.
  bool drain_buffered_event_timestamps(RX8111TimestampRecord *records, uint8_t max_records, uint8_t *count_out);

  /// Clears the timestamp RAM pointer without touching EVF.
  bool clear_timestamp_buffer();

  /// Chip-specific status accessors (valid after poll_flags())
  bool xst_flag() const { return this->xst_; }
  bool battery_low() const { return this->battery_low_; }
  bool evin_state() const { return this->evin_; }

  const char *model_name() const override { return "RX8111CE"; }

 protected:
  // ---- Virtual overrides ---------------------------------------------------
  uint8_t flag_reg_addr() const override { return RX8111_REG_FLAG; }
  uint8_t extension_reg_addr() const override { return RX8111_REG_EXTENSION; }
  uint8_t control_reg_addr() const override { return RX8111_REG_CONTROL; }

  uint8_t vlf_flag_mask() const override { return RX8111_FLAG_VLF; }
  uint8_t stop_bit_mask() const override { return RX8111_CTRL_STOP; }
  uint8_t alarm_flag_mask() const override { return RX8111_FLAG_AF; }
  uint8_t timer_flag_mask() const override { return RX8111_FLAG_TF; }
  uint8_t event_flag_mask() const override { return RX8111_FLAG_EVF; }
  bool supports_alarm_second_() const override { return true; }

  bool check_and_clear_vlf() override;
  bool configure_battery_backup() override;
  bool configure_alarm() override;
  bool configure_timer() override;
  bool disable_alarm() override;
  bool disable_timer() override;
  bool configure_event() override;
  bool disable_event() override;
  bool apply_runtime_options() override;

  void read_chip_status(uint8_t flag_byte) override;

 private:
  bool timestamp_capture_active() const;
  bool buffered_timestamp_mode() const;
  bool write_timestamp_mode_bits();
  bool clear_timestamp_ram();

  bool timestamp_enabled_{false};
  TimestampRecordMode timestamp_record_mode_{TIMESTAMP_RECORD_LATEST};
  EventLevel event_level_{EVENT_LEVEL_LOW};
  uint8_t evin_filter_{0};
  EvinPull evin_pull_{EVIN_PULL_NONE};
  bool xst_{false};
  bool battery_low_{false};
  bool evin_{false};
};

}  // namespace rx8xxx
