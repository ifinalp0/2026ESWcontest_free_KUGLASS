#pragma once

#include <cstdint>

// B-local UART pins. Logic Carrier does not route GPIO43/44 to ESP32_A, so an
// explicit external harness is required. Runtime console output is disabled to
// avoid sharing these pins and GPIO19/20 with UART0/native USB.
#ifndef KUGLASS_A_UART_TX_GPIO
#define KUGLASS_A_UART_TX_GPIO 43
#endif

#ifndef KUGLASS_A_UART_RX_GPIO
#define KUGLASS_A_UART_RX_GPIO 44
#endif

static constexpr uint8_t KUGLASS_CHANNEL_COUNT = 4;
static constexpr uint32_t KUGLASS_DEFAULT_TTL_MS = 250;
static constexpr uint32_t KUGLASS_OUTPUT_UPDATE_MS = 1;
// EN_GLOBAL always cuts CHx_ENABLE in hardware, and its falling-edge ISR
// immediately lowers all MCU ENABLE requests. A reset-required ESTOP latch is
// promoted only after ten consecutive output-task samples remain LOW. A
// shorter event must then remain HIGH for ten consecutive samples before it
// may be released as a glitch and wait for a fresh full command.
static constexpr uint8_t KUGLASS_ESTOP_CONFIRM_SAMPLES = 10;
static constexpr uint8_t KUGLASS_ESTOP_RELEASE_SAMPLES = 10;
// A raw FAULT_N falling edge still cuts the channel immediately. Ten
// consecutive output-task samples prevent brief or intermittent LOW noise
// from becoming a reset-required latched fault. Any intervening HIGH restarts
// qualification from zero.
static constexpr uint8_t KUGLASS_FAULT_CONFIRM_SAMPLES = 10;
// A short FAULT_N pulse can disappear as soon as RUN_OK and the MCU ENABLE
// request cut the stage. Allow one isolated pulse to recover after a stable
// HIGH interval, but never retry an unstable stage forever. Two falling edges
// inside this window latch the affected channel and require reset.
static constexpr uint8_t KUGLASS_FAULT_RELEASE_SAMPLES = 10;
static constexpr uint8_t KUGLASS_FAULT_REPEAT_EVENT_LIMIT = 2;
static constexpr uint32_t KUGLASS_FAULT_REPEAT_WINDOW_MS = 5000;
static constexpr uint32_t KUGLASS_DIRECTION_BLANKING_MS = 1;
static constexpr uint32_t KUGLASS_SAFETY_WATCHDOG_MS = 100;
static constexpr uint32_t KUGLASS_ANALOG_SCAN_PERIOD_MS = 5;
static constexpr float KUGLASS_CARRIER_HZ = 16000.0f;
static constexpr float KUGLASS_FUNDAMENTAL_HZ = 60.0f;
// The IRS2104 high-side supplies on the referenced Power Stage are
// bootstrapped. Keep at least 5% of every carrier period available for
// refresh until the production stage is characterized on the bench.
static constexpr float KUGLASS_MAX_MODULATION_INDEX = 0.95f;
static constexpr float KUGLASS_MI_ATTACK_PER_S = 2.4f;
static constexpr float KUGLASS_MI_RELEASE_PER_S = 0.7f;

static_assert(KUGLASS_MAX_MODULATION_INDEX > 0.0f &&
                  KUGLASS_MAX_MODULATION_INDEX < 1.0f,
              "Bootstrap PWM requires a non-zero carrier off-time.");
static_assert(KUGLASS_DIRECTION_BLANKING_MS >= KUGLASS_OUTPUT_UPDATE_MS,
              "Direction changes require at least one safe output update.");
static_assert(KUGLASS_ESTOP_CONFIRM_SAMPLES >= 2,
              "E-Stop qualification must reject one-sample LOW glitches.");
static_assert(KUGLASS_ESTOP_RELEASE_SAMPLES >= 2,
              "E-Stop glitch recovery requires a stable HIGH interval.");
static_assert(KUGLASS_FAULT_CONFIRM_SAMPLES >= 2,
              "Fault qualification must reject at least one-sample glitches.");
static_assert(KUGLASS_FAULT_RELEASE_SAMPLES >= 2,
              "Fault glitch recovery requires a stable HIGH interval.");
static_assert(KUGLASS_FAULT_REPEAT_EVENT_LIMIT >= 2,
              "One isolated FAULT_N pulse may recover without a latch.");
static_assert(KUGLASS_FAULT_REPEAT_WINDOW_MS >
                  KUGLASS_FAULT_RELEASE_SAMPLES * KUGLASS_OUTPUT_UPDATE_MS,
              "Repeated-fault window must outlive HIGH qualification.");
