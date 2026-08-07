#include "channel_manager.h"
#include "fault_manager.h"
#include "status_reporter.h"

#include <cstddef>
#include <cstdint>

extern "C" bool build_b_status_fixture(char* output, std::size_t output_size) {
    ChannelManager channels;
    channels.begin();
    FaultManager fault;
    fault.begin();

    AnalogTelemetrySnapshot analog;
    analog.initialized = true;
    analog.current_calibrated = true;
    analog.temperature_calibrated = true;
    analog.raw_valid_mask = 0xFF;
    analog.mv_valid_mask = 0xFF;
    for (std::size_t channel = 0; channel < KUGLASS_CHANNEL_COUNT; ++channel) {
        analog.current_raw[channel] = 4095;
        analog.temperature_raw[channel] = 4095;
        analog.current_mv[channel] = 5000;
        analog.temperature_mv[channel] = 5000;
    }

    StatusMetadata metadata;
    metadata.boot_id = UINT32_MAX;
    metadata.reset_challenge = UINT32_MAX - 1U;
    return format_status_line(1234, channels, fault, false, metadata,
                              &analog, "BOOT", output, output_size);
}
