#pragma once

#include "kuglass_config.h"
#include "protocol.h"

#include <cstddef>
#include <cstdint>

struct ChannelRuntime {
    uint8_t global_id = 0;
    float target_mi = 0.0f;
    float applied_mi = 0.0f;
    bool enable = false;
    bool faulted = false;
    float vrms = 0.0f;
    float irms = 0.0f;
    float temp_c = 25.0f;
};

class ChannelManager {
public:
    void begin(uint8_t first_channel);
    void apply_command(const ProtocolCommand& command);
    void update(float dt_s, bool global_enable);
    ChannelRuntime* local(size_t index);
    const ChannelRuntime* local(size_t index) const;
    size_t count() const { return KUGLASS_LOCAL_CHANNELS; }
    uint8_t first_channel() const { return first_channel_; }

private:
    int local_index_for_global(uint8_t global_id) const;

    uint8_t first_channel_ = KUGLASS_FIRST_CHANNEL;
    ChannelRuntime channels_[KUGLASS_LOCAL_CHANNELS];
};

