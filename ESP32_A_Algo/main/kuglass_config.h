#pragma once

#include <cstdint>

// ESP32_A is the algorithm/master node. It owns the integrated camera service
// and forwards all four MI targets to ESP32_B.
#ifndef KUGLASS_ONBOARD_CAMERA
#define KUGLASS_ONBOARD_CAMERA 1
#endif

// LIVE builds must not let a tablet overwrite physical sensor or fault state.
// Enable this only for an explicitly labelled MOCK/HIL firmware image.
#ifndef KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS
#define KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS 0
#endif

#ifndef KUGLASS_B_UART_TX_GPIO
#define KUGLASS_B_UART_TX_GPIO 39
#endif

#ifndef KUGLASS_B_UART_RX_GPIO
#define KUGLASS_B_UART_RX_GPIO 40
#endif

#ifndef KUGLASS_DS18B20_GPIO
#define KUGLASS_DS18B20_GPIO 41
#endif

// Runtime reset still requires a validated B boot_id/reset_challenge context
// and an exactly correlated control_result before ESP32_A reports success.
#ifndef KUGLASS_B_SUPPORTS_RESET_FAULT
#define KUGLASS_B_SUPPORTS_RESET_FAULT 1
#endif

static constexpr uint8_t KUGLASS_TOTAL_CHANNELS = 4;
static constexpr uint32_t KUGLASS_DEFAULT_TTL_MS = 200;
static constexpr uint32_t KUGLASS_DOWNSTREAM_TTL_MS = 250;
static constexpr uint32_t KUGLASS_CONTROL_PERIOD_MS = 50;
static constexpr uint32_t KUGLASS_CAMERA_STALE_MS = 1000;
static constexpr uint32_t KUGLASS_CAMERA_RETRY_MS = 2000;
static constexpr uint32_t KUGLASS_CAMERA_RETRY_MAX_MS = 30000;
static constexpr uint8_t KUGLASS_CAMERA_FAILURES_BEFORE_RESTART = 2;
static constexpr uint32_t KUGLASS_TEMPERATURE_STALE_MS = 5000;
static constexpr uint32_t KUGLASS_B_STATUS_TIMEOUT_MS = 1000;
static constexpr uint32_t KUGLASS_B_RESET_TIMEOUT_MS = 1500;
static constexpr float KUGLASS_CARRIER_HZ = 16000.0f;
static constexpr float KUGLASS_FUNDAMENTAL_HZ = 60.0f;
static constexpr float KUGLASS_DC_LINK_NOMINAL_V = 72.0f;
static constexpr float KUGLASS_MI_ATTACK_PER_S = 2.4f;
static constexpr float KUGLASS_MI_RELEASE_PER_S = 0.7f;
