#pragma once

#include <cstdint>

#ifndef KUGLASS_CONTROLLER_ID
#define KUGLASS_CONTROLLER_ID "A"
#endif

#ifndef KUGLASS_FIRST_CHANNEL
#define KUGLASS_FIRST_CHANNEL 0
#endif

static constexpr uint8_t KUGLASS_LOCAL_CHANNELS = 4;
static constexpr uint32_t KUGLASS_DEFAULT_TTL_MS = 200;
static constexpr float KUGLASS_CARRIER_HZ = 16000.0f;
static constexpr float KUGLASS_FUNDAMENTAL_HZ = 60.0f;
static constexpr float KUGLASS_DC_LINK_NOMINAL_V = 72.0f;
static constexpr float KUGLASS_MI_ATTACK_PER_S = 2.4f;
static constexpr float KUGLASS_MI_RELEASE_PER_S = 0.7f;

