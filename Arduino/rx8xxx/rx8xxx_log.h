#pragma once

// Platform-independent RX8XXX RTC library — optional logging
// Assign rx8xxx::rx8xxx_log_func at startup to enable log output.
// When nullptr (default), all logging is silently skipped.

#include <stdarg.h>
#include <stdint.h>

namespace rx8xxx {

enum RX8xxxLogLevel : uint8_t {
  RX8_LOG_ERROR = 0,
  RX8_LOG_WARN  = 1,
  RX8_LOG_INFO  = 2,
  RX8_LOG_DEBUG = 3,
};

/// User-assignable log function. Set to nullptr (default) to disable logging.
typedef void (*rx8xxx_log_func_t)(RX8xxxLogLevel level, const char *tag, const char *fmt, ...);
extern rx8xxx_log_func_t rx8xxx_log_func;

// Internal macro used throughout the library. Zero-cost when rx8xxx_log_func is nullptr.
// Uses the GNU ##__VA_ARGS__ extension to allow zero variadic arguments. This is supported
// by GCC, Clang, ARM Compiler 6, IAR, and MSVC — all compilers used in embedded development.
#define RX8_LOG(level, tag, fmt, ...) \
    do { if (::rx8xxx::rx8xxx_log_func) ::rx8xxx::rx8xxx_log_func(level, tag, fmt, ##__VA_ARGS__); } while(0)

}  // namespace rx8xxx
