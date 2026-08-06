#pragma once

#include "kuglass_b_config.h"
#include "protocol.h"

#include <cstddef>
#include <cstdint>

struct ChannelRuntime {
    uint8_t channel_id = 0;
    float target_mi = 0.0f;
    float applied_mi = 0.0f;
    bool enable = false;
    bool faulted = false;
};

class ChannelManager {
public:
    void begin();
    bool apply_command(const ProtocolCommand& command);
    void update(float dt_s, bool global_enable);
    void set_fault(size_t index, bool faulted);
    void clear_faults();
    ChannelRuntime* channel(size_t index);
    const ChannelRuntime* channel(size_t index) const;
    size_t count() const { return KUGLASS_CHANNEL_COUNT; }

private:
    ChannelRuntime channels_[KUGLASS_CHANNEL_COUNT];
};
