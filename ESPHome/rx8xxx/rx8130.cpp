#include "rx8130.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rx8xxx {

static const char *const TAG = "rx8130";

// ---------------------------------------------------------------------------
// dump_config
// ---------------------------------------------------------------------------
void RX8130Component::dump_config() {
  this->RX8XXXComponent::dump_config();
  if (this->use_digital_offset_) {
    ESP_LOGCONFIG(TAG, "  Digital Offset: %d", this->digital_offset_);
  }
  binary_sensor::log_binary_sensor(TAG, "  ", "Battery Low (VBFF)", this->battery_low_sensor_);
  binary_sensor::log_binary_sensor(TAG, "  ", "VDD Level Low (VBLF)", this->vblf_sensor_);
}

// ---------------------------------------------------------------------------
// check_and_clear_vlf_
//
// Reads the flag register (0x1D) and warns on:
//   - VLF  (bit1): oscillation lost / power failure; time data invalid.
//   - VBFF (bit0): backup battery (VBAT) voltage is critically low.
//   - VBLF (bit7): VDD supply battery level is low.
// Clears VLF and RSF. Preserves AF, TF, UF for update() polling.
// VBFF and VBLF are reported but NOT cleared here - they are live status flags
// that will be published as binary sensors from update_chip_binary_sensors_().
// Returns false only on I2C failure.
// ---------------------------------------------------------------------------
bool RX8130Component::check_and_clear_vlf_() {
  uint8_t flags = 0;
  if (!this->read_byte(RX8130_REG_FLAG, &flags)) {
    ESP_LOGE(TAG, "Can't read flag register");
    return false;
  }

  if (flags & RX8130_FLAG_VLF) {
    ESP_LOGW(TAG, "VLF set - oscillation was lost or power failed. "
                  "Time data invalid; write a valid time before use.");
  }
  if (flags & RX8130_FLAG_VBFF) {
    ESP_LOGW(TAG, "VBFF set - backup battery (VBAT) voltage is critically low.");
  }
  if (flags & RX8130_FLAG_VBLF) {
    ESP_LOGW(TAG, "VBLF set - VDD supply battery level is low.");
  }

  // Clear VLF and RSF only. Preserve AF, TF, UF for update() polling.
  // VBFF and VBLF remain for binary sensor reporting.
  uint8_t cleared = flags & ~(RX8130_FLAG_VLF | RX8130_FLAG_RSF);
  if (!this->write_byte(RX8130_REG_FLAG, cleared)) {
    ESP_LOGE(TAG, "Can't write flag register");
    return false;
  }

  // Apply digital offset if configured.
  if (this->use_digital_offset_) {
    // Register 0x30: bit7 = DTE (offset enable), bits6:0 = signed offset L7..L1.
    uint8_t offset_reg = 0x80 | (static_cast<uint8_t>(this->digital_offset_) & 0x7F);
    if (!this->write_byte(RX8130_REG_DIGOFFSET, offset_reg)) {
      ESP_LOGW(TAG, "Can't write digital offset register");
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// configure_battery_backup_
//
// Control Register 1 (0x1F):
//   bit5 = CHGEN  (trickle charge for rechargeable backup)
//   bit4 = INIEN  (automatic power switch enable)
// ---------------------------------------------------------------------------
bool RX8130Component::configure_battery_backup_() {
  uint8_t ctrl1 = 0;
  if (!this->read_byte(RX8130_REG_CONTROL1, &ctrl1)) {
    ESP_LOGE(TAG, "Can't read control register 1");
    return false;
  }

  ctrl1 |= RX8130_CTRL1_INIEN;

  if (this->battery_charging_) {
    ctrl1 |= RX8130_CTRL1_CHGEN;
  } else {
    ctrl1 &= ~RX8130_CTRL1_CHGEN;
  }

  if (!this->write_byte(RX8130_REG_CONTROL1, ctrl1)) {
    ESP_LOGE(TAG, "Can't write control register 1");
    return false;
  }

  ESP_LOGD(TAG, "Battery backup enabled (INIEN=1, CHGEN=%d)", this->battery_charging_ ? 1 : 0);
  return true;
}

// ---------------------------------------------------------------------------
// configure_alarm_
//
// Alarm registers:
//   0x17  MIN alarm  (AE bit7; BCD minutes 00-59)
//   0x18  HOUR alarm (AE bit7; BCD hours  00-23)
//   0x19  WEEK/DAY alarm (AE bit7; one-hot weekday OR BCD day, WADA selects)
//
// RX8130CE does NOT have a second alarm register.
//
// IMPORTANT: The alarm /IRQ output on RX8130CE is NOT auto-cleared after
// firing. It stays low until the user explicitly clears AF via
// rx8xxx.clear_alarm_flag. Reconfiguring the alarm does NOT clear AF.
// ---------------------------------------------------------------------------
bool RX8130Component::configure_alarm_() {
  // Keep AIE asserted while updating alarm registers so runtime reprogramming
  // does not deassert /IRQ when AF is already latched.
  uint8_t ctrl0 = 0;
  if (!this->read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;

  // --- Minute alarm (0x17) ---
  uint8_t min_alarm = RX8XXX_ALARM_AE;
  if (this->alarm_minute_ != 0xFF) {
    min_alarm = uint8_to_bcd_(this->alarm_minute_);
  }
  if (!this->write_byte(RX8XXX_REG_MIN_ALARM, min_alarm)) return false;

  // --- Hour alarm (0x18) ---
  uint8_t hour_alarm = RX8XXX_ALARM_AE;
  if (this->alarm_hour_ != 0xFF) {
    hour_alarm = uint8_to_bcd_(this->alarm_hour_);
  }
  if (!this->write_byte(RX8XXX_REG_HOUR_ALARM, hour_alarm)) return false;

  // --- Week / Day alarm (0x19) and WADA bit ---
  uint8_t ext = 0;
  if (!this->read_byte(RX8130_REG_EXTENSION, &ext)) return false;

  uint8_t wd_alarm = RX8XXX_ALARM_AE;
  if (this->alarm_weekday_ != 0xFF) {
    // WEEK alarm mode: one-hot weekday, WADA=0
    wd_alarm = weekday_to_onehot_(this->alarm_weekday_);
    ext &= ~RX8130_EXT_WADA;
  } else if (this->alarm_day_ != 0xFF) {
    // DAY alarm mode: BCD day, WADA=1
    wd_alarm = uint8_to_bcd_(this->alarm_day_);
    ext |= RX8130_EXT_WADA;
  }
  if (!this->write_byte(RX8XXX_REG_WD_ALARM, wd_alarm)) return false;
  if (!this->write_byte(RX8130_REG_EXTENSION, ext)) return false;

  // Ensure AIE is enabled after programming. If already enabled, this is a no-op.
  ctrl0 |= RX8130_CTRL0_AIE;
  ctrl0 &= 0x7F;
  if (!this->write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// configure_timer_
//
// Timer counter is 16-bit: 0x1A (LSB), 0x1B (MSB).
// Source clock: TSEL2:TSEL1:TSEL0 in extension register 0x1C bits 2:0.
//   000 = 4096Hz, 001 = 64Hz, 010 = 1Hz, 011 = 1/60Hz, 100 = 1/3600Hz
// TE (0x1C bit4) starts the timer. TIE (0x1E bit4) enables interrupt.
//
// Per datasheet: clear TE first, write counter, then set TE.
//
// NOTE: The timer /IRQ auto-clears after ~7.57ms (tRTN2). The TF flag however
// stays set until cleared. configure_timer_() does NOT clear TF.
// ---------------------------------------------------------------------------
bool RX8130Component::configure_timer_() {
  // Step 1: stop timer and disable interrupt while reconfiguring.
  uint8_t ext = 0;
  if (!this->read_byte(RX8130_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8130_EXT_TE;
  if (!this->write_byte(RX8130_REG_EXTENSION, ext)) return false;

  uint8_t ctrl0 = 0;
  if (!this->read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;
  ctrl0 &= ~RX8130_CTRL0_TIE;
  ctrl0 &= 0x7F;  // never set TEST bit
  if (!this->write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  // Step 2: write 16-bit counter value; clamp to 16-bit max for this chip.
  uint32_t count = this->timer_count_;
  if (count > 0xFFFF) {
    ESP_LOGW(TAG, "Timer count %" PRIu32 " exceeds 16-bit max for RX8130CE; clamping to 65535", count);
    count = 0xFFFF;
  }
  uint8_t timer_buf[2] = {
      static_cast<uint8_t>(count & 0xFF),
      static_cast<uint8_t>((count >> 8) & 0xFF),
  };
  if (!this->write_bytes(RX8XXX_REG_TIMER0, timer_buf, sizeof(timer_buf))) return false;

  // Step 3: set source clock.
  uint8_t tsel = static_cast<uint8_t>(this->timer_clock_);
  if (tsel > 4) tsel = 4;
  ext &= ~RX8130_EXT_TSEL_MASK;
  ext |= (tsel & RX8130_EXT_TSEL_MASK);
  if (!this->write_byte(RX8130_REG_EXTENSION, ext)) return false;

  // Step 4: enable interrupt and start timer.
  ctrl0 |= RX8130_CTRL0_TIE;
  ctrl0 &= 0x7F;
  if (!this->write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  ext |= RX8130_EXT_TE;
  if (!this->write_byte(RX8130_REG_EXTENSION, ext)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// disable_alarm_
//
// Disables alarm matching and alarm interrupt generation without touching AF.
// ---------------------------------------------------------------------------
bool RX8130Component::disable_alarm_() {
  uint8_t ctrl0 = 0;
  if (!this->read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;
  ctrl0 &= ~RX8130_CTRL0_AIE;
  ctrl0 &= 0x7F;
  if (!this->write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  if (!this->write_byte(RX8XXX_REG_MIN_ALARM, RX8XXX_ALARM_AE)) return false;
  if (!this->write_byte(RX8XXX_REG_HOUR_ALARM, RX8XXX_ALARM_AE)) return false;
  if (!this->write_byte(RX8XXX_REG_WD_ALARM, RX8XXX_ALARM_AE)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// disable_timer_
//
// Disables timer interrupt (TIE=0) and timer engine (TE=0) without touching TF.
// ---------------------------------------------------------------------------
bool RX8130Component::disable_timer_() {
  uint8_t ctrl0 = 0;
  if (!this->read_byte(RX8130_REG_CONTROL0, &ctrl0)) return false;
  ctrl0 &= ~RX8130_CTRL0_TIE;
  ctrl0 &= 0x7F;
  if (!this->write_byte(RX8130_REG_CONTROL0, ctrl0)) return false;

  uint8_t ext = 0;
  if (!this->read_byte(RX8130_REG_EXTENSION, &ext)) return false;
  ext &= ~RX8130_EXT_TE;
  if (!this->write_byte(RX8130_REG_EXTENSION, ext)) return false;
  return true;
}

// ---------------------------------------------------------------------------
// update_chip_binary_sensors_
//
// VBFF and VBLF are both in the flag register already read by update().
// No extra I2C read needed.
// ---------------------------------------------------------------------------
void RX8130Component::update_chip_binary_sensors_(uint8_t flag_byte) {
  if (this->battery_low_sensor_ != nullptr) {
    this->battery_low_sensor_->publish_state((flag_byte & RX8130_FLAG_VBFF) != 0);
  }
  if (this->vblf_sensor_ != nullptr) {
    this->vblf_sensor_->publish_state((flag_byte & RX8130_FLAG_VBLF) != 0);
  }
}

}  // namespace rx8xxx
}  // namespace esphome
