#include "master_telemetry.h"

#include "kuglass_config.h"
#include "ui_protocol.h"

#include <cstdarg>
#include <cstdio>

namespace {

bool append(char* output, size_t output_size, size_t* used, const char* format, ...) {
    if (output == nullptr || used == nullptr || *used >= output_size) return false;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(output + *used, output_size - *used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= output_size - *used) {
        output[output_size - 1] = '\0';
        return false;
    }
    *used += static_cast<size_t>(written);
    return true;
}

uint32_t remaining_ms(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(deadline_ms - now_ms) > 0
               ? static_cast<uint32_t>(deadline_ms - now_ms)
               : 0U;
}

}  // namespace

bool format_master_state(const PolicyDecision& decision,
                         const SensorSnapshot& sensors,
                         uint32_t now_ms,
                         bool downstream_healthy,
                         const char* downstream_error,
                         char* output,
                         size_t output_size) {
    if (output == nullptr || output_size == 0) return false;
    output[0] = '\0';
    size_t used = 0;
    if (!append(output, output_size, &used,
                "{\"type\":\"state\",\"schema_version\":1,\"seq\":%lu,"
                "\"vehicle_mode\":\"%s\",\"demo_mode\":\"%s\","
                "\"timestamp_ms\":%lu,\"environment\":{",
                static_cast<unsigned long>(decision.seq),
                vehicle_mode_name(decision.vehicle_mode),
                demo_mode_name(decision.demo_mode),
                static_cast<unsigned long>(now_ms))) {
        return false;
    }
    if (sensors.internal_temp_valid) {
        if (!append(output, output_size, &used,
                    "\"internal_temp_c\":%.2f},", sensors.internal_temp_c)) return false;
    } else if (!append(output, output_size, &used, "\"internal_temp_c\":null},")) {
        return false;
    }

    const float edge_density =
        (sensors.front_left.edge_density + sensors.front_right.edge_density) * 0.5f;
    if (!append(output, output_size, &used,
                "\"camera_metrics\":{\"valid\":%s,\"ae_metadata_valid\":%s,"
                "\"front_left_saturation\":%.4f,"
                "\"front_right_saturation\":%.4f,\"edge_density\":%.4f,"
                "\"glare\":%.4f,\"frame_id\":%lu,\"timestamp_ms\":%lu},",
                sensors.camera_valid ? "true" : "false",
                sensors.ae_metadata_valid ? "true" : "false",
                sensors.front_left.saturation_ratio,
                sensors.front_right.saturation_ratio,
                edge_density,
                decision.glare_total,
                static_cast<unsigned long>(sensors.camera_frame_id),
                static_cast<unsigned long>(sensors.camera_timestamp_ms))) {
        return false;
    }

    if (!append(output, output_size, &used, "\"channels\":[")) return false;
    for (size_t i = 0; i < KUGLASS_TOTAL_CHANNELS; ++i) {
        const PolicyChannelTarget& channel = decision.channels[i];
        if (!append(output, output_size, &used,
                    "%s{\"channel_id\":%u,\"target_mi\":%.4f,\"commanded_mi\":%.4f,"
                    "\"applied_source\":\"awaiting_esp32_b\","
                    "\"estimated_transmittance\":%.4f,\"optical_state\":\"%s\","
                    "\"enable\":%s,\"fault\":%s,\"manual_active\":%s,"
                    "\"manual_persistent\":%s,\"manual_remaining_ms\":%lu}",
                    i == 0 ? "" : ",",
                    static_cast<unsigned>(channel.channel_id),
                    channel.target_mi,
                    channel.applied_mi,
                    channel.target_transmission,
                    optical_state_name(channel.optical_state),
                    channel.enable ? "true" : "false",
                    channel.fault ? "true" : "false",
                    channel.manual ? "true" : "false",
                    channel.manual_persistent ? "true" : "false",
                    static_cast<unsigned long>(
                        channel.manual && !channel.manual_persistent
                            ? remaining_ms(now_ms, channel.manual_until_ms)
                            : 0U))) {
            return false;
        }
    }
    return append(output, output_size, &used,
                  "],\"decision_reason\":\"%s\",\"thermal_risk\":%.4f,"
                  "\"downstream\":{\"controller_id\":\"B\",\"healthy\":%s,"
                  "\"error\":\"%s\"}}",
                  decision.decision_reason,
                  decision.thermal_risk,
                  downstream_healthy ? "true" : "false",
                  downstream_error == nullptr ? "UNKNOWN" : downstream_error);
}
