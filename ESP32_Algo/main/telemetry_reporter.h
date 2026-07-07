#pragma once

#include "channel_manager.h"
#include "fault_manager.h"

#include <cstdint>

class TelemetryReporter {
public:
    void report_status(const char* controller_id, uint32_t seq, const ChannelManager& channels, const FaultManager& fault);
};

