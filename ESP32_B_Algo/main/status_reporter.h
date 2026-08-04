#pragma once

#include "analog_monitor.h"
#include "channel_manager.h"
#include "fault_manager.h"

#include <cstddef>
#include <cstdint>

struct ResetControlResult {
    uint32_t seq = 0;
    uint32_t source_session_id = 0;
    bool ok = false;
    const char* error = nullptr;
};

struct StatusMetadata {
    uint32_t boot_id = 0;
    uint32_t reset_challenge = 0;
    const ResetControlResult* control_result = nullptr;
};

bool format_status_line(uint32_t seq,
                        const ChannelManager& channels,
                        const FaultManager& fault,
                        bool estop_active,
                        const StatusMetadata& metadata,
                        const AnalogTelemetrySnapshot* analog,
                        const char* diagnostic,
                        char* output,
                        size_t output_size);

inline bool format_status_line(uint32_t seq,
                               const ChannelManager& channels,
                               const FaultManager& fault,
                               bool estop_active,
                               const StatusMetadata& metadata,
                               const AnalogTelemetrySnapshot* analog,
                               char* output,
                               size_t output_size) {
    return format_status_line(seq, channels, fault, estop_active, metadata,
                              analog, nullptr, output, output_size);
}

inline bool format_status_line(uint32_t seq,
                               const ChannelManager& channels,
                               const FaultManager& fault,
                               bool estop_active,
                               const StatusMetadata& metadata,
                               char* output,
                               size_t output_size) {
    return format_status_line(seq, channels, fault, estop_active, metadata,
                              nullptr, nullptr, output, output_size);
}
