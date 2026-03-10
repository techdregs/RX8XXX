#pragma once

#include "rx8xxx.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
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
static const uint8_t RX8111_EVIN_ET_MASK   = 0x60;  ///< ET1:ET0 chattering filter mask (bits 6:5)
static const uint8_t RX8111_EVIN_ET_SHIFT  = 5;     ///< ET1:ET0 bit shift
static const uint8_t RX8111_EVIN_PDN       = 0x10;  ///< Pull-down select
static const uint8_t RX8111_EVIN_PU1       = 0x08;  ///< Pull-up bit 1
static const uint8_t RX8111_EVIN_PU0       = 0x04;  ///< Pull-up bit 0
static const uint8_t RX8111_EVIN_OVW       = 0x02;  ///< Timestamp overwrite mode

// ---------------------------------------------------------------------------
// RX8111CE timestamp control register 1 (0x34) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8111_TSCTRL1_EISEL  = 0x04;  ///< Event Interrupt SELect (1=interrupt after 8 records, 0=interrupt on each event)
static const uint8_t RX8111_TSCTRL1_TSCLR  = 0x02;  ///< Clear timestamp RAM pointer (auto-resets)
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
// RX8111Component
// ---------------------------------------------------------------------------
class RX8111Component : public RX8XXXComponent {
 public:
  void dump_config() override;

  // RX8111CE-specific binary sensor setters
  void set_xst_binary_sensor(binary_sensor::BinarySensor *s) { this->xst_sensor_ = s; }
  void set_battery_low_binary_sensor(binary_sensor::BinarySensor *s) { this->battery_low_sensor_ = s; }
  void set_evin_binary_sensor(binary_sensor::BinarySensor *s) { this->evin_sensor_ = s; }

  // Enable hardware event timestamp capture independently of EVF interrupting.
  void set_timestamp_enabled(bool enable) { this->timestamp_enabled_ = enable; }
  void set_timestamp_record_mode(TimestampRecordMode mode) { this->timestamp_record_mode_ = mode; }
  void set_event_timestamp_text_sensor(text_sensor::TextSensor *s) { this->event_timestamp_sensor_ = s; }

 protected:
  // ---- Virtual overrides ---------------------------------------------------
  uint8_t flag_reg_addr_() override { return RX8111_REG_FLAG; }
  uint8_t extension_reg_addr_() override { return RX8111_REG_EXTENSION; }
  uint8_t control_reg_addr_() override { return RX8111_REG_CONTROL; }

  uint8_t vlf_flag_mask_() override { return RX8111_FLAG_VLF; }
  uint8_t stop_bit_mask_() override { return RX8111_CTRL_STOP; }
  uint8_t alarm_flag_mask_() override { return RX8111_FLAG_AF; }
  uint8_t timer_flag_mask_() override { return RX8111_FLAG_TF; }
  uint8_t event_flag_mask_() override { return RX8111_FLAG_EVF; }
  bool supports_alarm_second_() const override { return true; }

  const char *model_name_() override { return "RX8111CE"; }

  bool check_and_clear_vlf_() override;
  bool configure_battery_backup_() override;
  bool configure_alarm_() override;
  bool configure_timer_() override;
  bool disable_alarm_() override;
  bool disable_timer_() override;
  bool configure_event_() override;
  bool disable_event_() override;
  bool apply_runtime_options_() override;
  void after_initial_time_read_() override;

  /// Reads status register (0x33) for VLOW and EVIN; publishes chip-specific sensors.
  void update_chip_binary_sensors_(uint8_t flag_byte) override;

  bool publish_timestamp_records_(uint8_t flag_byte);

  // ---- RX8111CE-specific binary sensor sub-components ---------------------
  binary_sensor::BinarySensor *xst_sensor_{nullptr};         ///< Crystal stop flag (flag reg bit0)
  binary_sensor::BinarySensor *battery_low_sensor_{nullptr}; ///< VBAT low (status reg 0x33 bit1)
  binary_sensor::BinarySensor *evin_sensor_{nullptr};        ///< EVIN pin state (status reg 0x33 bit6)
  text_sensor::TextSensor *event_timestamp_sensor_{nullptr}; ///< Published RTC event timestamps

  bool timestamp_enabled_{false};
  TimestampRecordMode timestamp_record_mode_{TIMESTAMP_RECORD_LATEST};
  bool has_last_latest_timestamp_{false};
  std::array<uint8_t, 10> last_latest_timestamp_raw_{};

  bool timestamp_capture_active_() const;
  bool buffered_timestamp_mode_() const;
  bool write_timestamp_mode_bits_();
  bool clear_timestamp_ram_();
};

}  // namespace rx8xxx
}  // namespace esphome
