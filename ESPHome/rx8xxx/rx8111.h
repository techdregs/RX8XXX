#pragma once

#include "rx8xxx.h"

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
// RX8111Component
// ---------------------------------------------------------------------------
class RX8111Component : public RX8XXXComponent {
 public:
  void dump_config() override;

  // RX8111CE-specific binary sensor setters
  void set_xst_binary_sensor(binary_sensor::BinarySensor *s) { this->xst_sensor_ = s; }
  void set_battery_low_binary_sensor(binary_sensor::BinarySensor *s) { this->battery_low_sensor_ = s; }
  void set_evin_binary_sensor(binary_sensor::BinarySensor *s) { this->evin_sensor_ = s; }

  // Enable time stamp storage (0x40-0x7F used for stamps instead of user RAM)
  void set_timestamp_enabled(bool enable) { this->timestamp_enabled_ = enable; }

 protected:
  // ---- Virtual overrides ---------------------------------------------------
  uint8_t flag_reg_addr_() override { return RX8111_REG_FLAG; }
  uint8_t extension_reg_addr_() override { return RX8111_REG_EXTENSION; }
  uint8_t control_reg_addr_() override { return RX8111_REG_CONTROL; }

  uint8_t vlf_flag_mask_() override { return RX8111_FLAG_VLF; }
  uint8_t stop_bit_mask_() override { return RX8111_CTRL_STOP; }
  uint8_t alarm_flag_mask_() override { return RX8111_FLAG_AF; }
  uint8_t timer_flag_mask_() override { return RX8111_FLAG_TF; }
  bool supports_alarm_second_() const override { return true; }

  const char *model_name_() override { return "RX8111CE"; }

  bool check_and_clear_vlf_() override;
  bool configure_battery_backup_() override;
  bool configure_alarm_() override;
  bool configure_timer_() override;
  bool disable_alarm_() override;
  bool disable_timer_() override;
  bool apply_runtime_options_() override;

  /// Reads status register (0x33) for VLOW and EVIN; publishes chip-specific sensors.
  void update_chip_binary_sensors_(uint8_t flag_byte) override;

  // ---- RX8111CE-specific binary sensor sub-components ---------------------
  binary_sensor::BinarySensor *xst_sensor_{nullptr};         ///< Crystal stop flag (flag reg bit0)
  binary_sensor::BinarySensor *battery_low_sensor_{nullptr}; ///< VBAT low (status reg 0x33 bit1)
  binary_sensor::BinarySensor *evin_sensor_{nullptr};        ///< EVIN pin state (status reg 0x33 bit6)

  bool timestamp_enabled_{false};
};

}  // namespace rx8xxx
}  // namespace esphome
