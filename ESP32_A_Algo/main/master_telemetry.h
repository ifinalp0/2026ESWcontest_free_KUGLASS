#pragma once

#include "policy_engine.h"
#include "sensor_snapshot.h"

#include <cstddef>
#include <cstdint>

bool format_master_state(const PolicyDecision& decision,
                         const SensorSnapshot& sensors,
                         uint32_t now_ms,
                         bool downstream_healthy,
                         const char* downstream_error,
                         char* output,
                         size_t output_size);
