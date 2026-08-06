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
// TEST BUILD ONLY: generate the actuator command locally so ESP32_B GPIO
// waveforms can be measured without ESP32_A. Physical EN_GLOBAL and FAULT_N
// inputs remain authoritative even in this standalone build.
static constexpr bool KUGLASS_STANDALONE_OUTPUT_TEST = true;
static constexpr float KUGLASS_STANDALONE_TEST_MI = 0.75f;
static constexpr bool KUGLASS_STANDALONE_SAFETY_INPUTS_ENABLED = true;
// Keep the standalone waveform source deterministic. Floating UART input,
// periodic JSON status traffic and disconnected ADC inputs are intentionally
// excluded from this build unless a bench test explicitly opts them back in.
static constexpr bool KUGLASS_STANDALONE_UART_ENABLED = false;
static constexpr bool KUGLASS_STANDALONE_ANALOG_ENABLED = false;
// Let board rails and GPIO output latches settle before the MI ramp begins.
static constexpr uint32_t KUGLASS_STANDALONE_STARTUP_SETTLE_MS = 250;
// The production 100 ms watchdog is intentionally strict. The isolated bench
// build uses a wider window to avoid nuisance resets caused by probes and
// debugger stalls while still forcing the raw enables low on a real timeout.
static constexpr uint32_t KUGLASS_STANDALONE_WATCHDOG_MS = 500;
static constexpr uint32_t KUGLASS_DEFAULT_TTL_MS = 250;
static constexpr uint32_t KUGLASS_OUTPUT_UPDATE_MS = 1;
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
static_assert(KUGLASS_STANDALONE_TEST_MI > 0.0f &&
                  KUGLASS_STANDALONE_TEST_MI <=
                      KUGLASS_MAX_MODULATION_INDEX,
              "Standalone test MI must produce a valid PWM duty.");
static_assert(KUGLASS_STANDALONE_STARTUP_SETTLE_MS >=
                  KUGLASS_OUTPUT_UPDATE_MS,
              "Standalone startup must include at least one safe update.");
static_assert(KUGLASS_STANDALONE_WATCHDOG_MS >
                  KUGLASS_SAFETY_WATCHDOG_MS,
              "Standalone watchdog should be less sensitive than production.");
static_assert(KUGLASS_DIRECTION_BLANKING_MS >= KUGLASS_OUTPUT_UPDATE_MS,
              "Direction changes require at least one safe output update.");
