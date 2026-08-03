#pragma once

#include <cstdint>

#ifndef KUGLASS_A_UART_TX_GPIO
#define KUGLASS_A_UART_TX_GPIO 39
#endif

#ifndef KUGLASS_A_UART_RX_GPIO
#define KUGLASS_A_UART_RX_GPIO 40
#endif

static constexpr uint8_t KUGLASS_CHANNEL_COUNT = 4;
static constexpr uint32_t KUGLASS_DEFAULT_TTL_MS = 250;
static constexpr float KUGLASS_CARRIER_HZ = 16000.0f;
static constexpr float KUGLASS_FUNDAMENTAL_HZ = 60.0f;
static constexpr float KUGLASS_MI_ATTACK_PER_S = 2.4f;
static constexpr float KUGLASS_MI_RELEASE_PER_S = 0.7f;
