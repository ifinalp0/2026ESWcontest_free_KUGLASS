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
static constexpr float KUGLASS_CARRIER_HZ = 16000.0f;
static constexpr float KUGLASS_FUNDAMENTAL_HZ = 60.0f;
static constexpr float KUGLASS_MI_ATTACK_PER_S = 2.4f;
static constexpr float KUGLASS_MI_RELEASE_PER_S = 0.7f;
