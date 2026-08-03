#pragma once

#include "channel_manager.h"
#include "fault_manager.h"

#include <cstddef>
#include <cstdint>

bool format_status_line(uint32_t seq,
                        const ChannelManager& channels,
                        const FaultManager& fault,
                        bool estop_active,
                        char* output,
                        size_t output_size);
