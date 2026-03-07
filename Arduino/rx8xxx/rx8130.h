#pragma once

// Platform-independent RX8XXX RTC library — RX8130CE implementation

#include "rx8xxx_base.h"

namespace rx8xxx {

// ---------------------------------------------------------------------------
// RX8130CE register addresses
// ---------------------------------------------------------------------------
static const uint8_t RX8130_REG_EXTENSION  = 0x1C;  ///< FSEL1,FSEL0,USEL,TE,WADA,TSEL2,TSEL1,TSEL0
static const uint8_t RX8130_REG_FLAG       = 0x1D;  ///< VBLF,0,UF,TF,AF,RSF,VLF,VBFF
static const uint8_t RX8130_REG_CONTROL0   = 0x1E;  ///< TEST(must=0!),STOP,UIE,TIE,AIE,TSTP,TBKON,TBKE
static const uint8_t RX8130_REG_CONTROL1   = 0x1F;  ///< SMPTSEL1,SMPTSEL0,CHGEN,INIEN,0,RS,BFVSEL1,BFVSEL0
static const uint8_t RX8130_REG_USERRAM    = 0x20;  ///< 4-byte general-purpose user RAM (0x20-0x23)
static const uint8_t RX8130_REG_DIGOFFSET  = 0x30;  ///< Digital frequency offset: DTE,L7..L1
static const uint8_t RX8130_REG_EXTREG1    = 0x31;  ///< Extension Register 1: VBLFE (bit0)

// ---------------------------------------------------------------------------
// RX8130CE flag register (0x1D) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8130_FLAG_VBLF = 0x80;  ///< VDD Battery Level Low Flag
static const uint8_t RX8130_FLAG_UF   = 0x20;  ///< Time Update Flag
static const uint8_t RX8130_FLAG_TF   = 0x10;  ///< Timer Flag
static const uint8_t RX8130_FLAG_AF   = 0x08;  ///< Alarm Flag
static const uint8_t RX8130_FLAG_RSF  = 0x04;  ///< Software Reset Flag
static const uint8_t RX8130_FLAG_VLF  = 0x02;  ///< Voltage Low Flag (oscillation lost)
static const uint8_t RX8130_FLAG_VBFF = 0x01;  ///< Backup Battery Failure Flag (VBAT low)

// ---------------------------------------------------------------------------
// RX8130CE Control Register 0 (0x1E) bit masks
// IMPORTANT: bit7 = TEST pin. Must NEVER be written as 1. Always mask with 0x7F.
// ---------------------------------------------------------------------------
static const uint8_t RX8130_CTRL0_STOP  = 0x40;  ///< Stop bit (bit6) - halts timekeeping
static const uint8_t RX8130_CTRL0_UIE   = 0x20;  ///< Update Interrupt Enable
static const uint8_t RX8130_CTRL0_TIE   = 0x10;  ///< Timer Interrupt Enable
static const uint8_t RX8130_CTRL0_AIE   = 0x08;  ///< Alarm Interrupt Enable
static const uint8_t RX8130_CTRL0_TSTP  = 0x04;  ///< Timer Stop (pauses accumulative timer)
static const uint8_t RX8130_CTRL0_TBKON = 0x02;  ///< Timer Backup Operation enable
static const uint8_t RX8130_CTRL0_TBKE  = 0x01;  ///< Timer Backup Keep Enable

// ---------------------------------------------------------------------------
// RX8130CE Control Register 1 (0x1F) bit masks
// ---------------------------------------------------------------------------
static const uint8_t RX8130_CTRL1_CHGEN  = 0x20;  ///< Charge Enable (rechargeable backup)
static const uint8_t RX8130_CTRL1_INIEN  = 0x10;  ///< Power Switch (interface) Enable
static const uint8_t RX8130_CTRL1_RS     = 0x04;  ///< Reset output control

// ---------------------------------------------------------------------------
// RX8130CE extension register (0x1C) bit masks / shifts
// ---------------------------------------------------------------------------
static const uint8_t RX8130_EXT_TE         = 0x10;  ///< Timer Enable
static const uint8_t RX8130_EXT_WADA       = 0x08;  ///< Week/Day Alarm select (1=DAY, 0=WEEK)
static const uint8_t RX8130_EXT_TSEL_MASK  = 0x07;  ///< TSEL2:TSEL1:TSEL0 in bits 2:0

// ---------------------------------------------------------------------------
// RX8130 — concrete implementation for RX8130CE
// ---------------------------------------------------------------------------
class RX8130 : public RX8xxxBase {
 public:
  /// Digital frequency offset: DTE enable bit + signed 7-bit offset (L7..L1).
  void set_digital_offset(int8_t offset) {
    this->digital_offset_ = offset;
    this->use_digital_offset_ = true;
  }

  /// Chip-specific status accessors (valid after poll_flags())
  bool vbff_flag() const { return this->vbff_; }
  bool vblf_flag() const { return this->vblf_; }

  const char *model_name() const override { return "RX8130CE"; }

 protected:
  // ---- Virtual overrides ---------------------------------------------------
  uint8_t flag_reg_addr() const override { return RX8130_REG_FLAG; }
  uint8_t extension_reg_addr() const override { return RX8130_REG_EXTENSION; }
  uint8_t control_reg_addr() const override { return RX8130_REG_CONTROL0; }

  uint8_t vlf_flag_mask() const override { return RX8130_FLAG_VLF; }
  uint8_t stop_bit_mask() const override { return RX8130_CTRL0_STOP; }
  uint8_t alarm_flag_mask() const override { return RX8130_FLAG_AF; }
  uint8_t timer_flag_mask() const override { return RX8130_FLAG_TF; }

  bool check_and_clear_vlf() override;
  bool configure_battery_backup() override;
  bool configure_alarm() override;
  bool configure_timer() override;
  bool disable_alarm() override;
  bool disable_timer() override;

  void read_chip_status(uint8_t flag_byte) override;

 private:
  bool use_digital_offset_{false};
  int8_t digital_offset_{0};
  bool vbff_{false};
  bool vblf_{false};
};

}  // namespace rx8xxx
