#include "status_reporter.h"

#include <cstdarg>
#include <cstdio>

namespace {

bool append(char* output, size_t size, size_t* used, const char* format, ...) {
    if (output == nullptr || used == nullptr || *used >= size) return false;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(output + *used, size - *used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= size - *used) return false;
    *used += static_cast<size_t>(written);
    return true;
}

bool token_is_json_safe(const char* token) {
    if (token == nullptr || token[0] == '\0') return false;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(token);
         *cursor != '\0'; ++cursor) {
        const bool valid = (*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= '0' && *cursor <= '9') || *cursor == '_';
        if (!valid) return false;
    }
    return true;
}

}  // namespace

bool format_status_line(uint32_t seq,
                        const ChannelManager& channels,
                        const FaultManager& fault,
                        bool estop_active,
                        const StatusMetadata& metadata,
                        const AnalogTelemetrySnapshot* analog,
                        const char* diagnostic,
                        char* output,
                        size_t output_size) {
    if (output == nullptr || output_size == 0U) return false;
    if (metadata.boot_id == 0U || metadata.reset_challenge == 0U) return false;
    if (metadata.control_result != nullptr &&
        (metadata.control_result->source_session_id == 0U ||
         !token_is_json_safe(metadata.control_result->error))) {
        return false;
    }
    output[0] = '\0';
    size_t used = 0;
    if (!append(output, output_size, &used,
                "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\","
                "\"seq\":%lu,\"boot_id\":%lu,\"reset_challenge\":%lu,"
                "\"estop\":%s,\"fault_code\":\"%s\"",
                static_cast<unsigned long>(seq),
                static_cast<unsigned long>(metadata.boot_id),
                static_cast<unsigned long>(metadata.reset_challenge),
                estop_active ? "true" : "false", fault.code_name())) return false;
    if (diagnostic != nullptr) {
        if (!token_is_json_safe(diagnostic) ||
            !append(output, output_size, &used, ",\"diagnostic\":\"%s\"",
                    diagnostic)) {
            return false;
        }
    }
    if (metadata.control_result != nullptr) {
        const ResetControlResult& result = *metadata.control_result;
        if (!append(output, output_size, &used,
                    ",\"control_result\":{\"command\":\"reset_fault\","
                    "\"seq\":%lu,\"source_session_id\":%lu,\"ok\":%s,"
                    "\"error\":\"%s\"}",
                    static_cast<unsigned long>(result.seq),
                    static_cast<unsigned long>(result.source_session_id),
                    result.ok ? "true" : "false", result.error)) {
            return false;
        }
    }
    if (!append(output, output_size, &used, ",\"ch\":[")) return false;
    for (size_t i = 0; i < channels.count(); ++i) {
        const ChannelRuntime* item = channels.channel(i);
        if (item == nullptr) return false;
        if (!append(output, output_size, &used,
                    "%s{\"id\":%u,\"mi\":%.4f,\"fault\":%s}",
                    i == 0U ? "" : ",", static_cast<unsigned>(item->channel_id),
                    item->applied_mi,
                    (fault.faulted() || item->faulted) ? "true" : "false")) return false;
    }
    if (analog == nullptr) {
        return append(output, output_size, &used, "]}");
    }

    if (!append(output, output_size, &used,
                "],\"adc\":{\"initialized\":%s,\"i_cali\":%s,"
                "\"t_cali\":%s,\"raw_valid_mask\":%u,"
                "\"mv_valid_mask\":%u,\"i_raw\":[",
                analog->initialized ? "true" : "false",
                analog->current_calibrated ? "true" : "false",
                analog->temperature_calibrated ? "true" : "false",
                static_cast<unsigned>(analog->raw_valid_mask),
                static_cast<unsigned>(analog->mv_valid_mask))) return false;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (!append(output, output_size, &used, "%s%d",
                    i == 0U ? "" : ",", analog->current_raw[i])) return false;
    }
    if (!append(output, output_size, &used, "],\"t_raw\":[")) return false;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (!append(output, output_size, &used, "%s%d",
                    i == 0U ? "" : ",", analog->temperature_raw[i])) return false;
    }
    if (!append(output, output_size, &used, "],\"i_mv\":[")) return false;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (!append(output, output_size, &used, "%s%d",
                    i == 0U ? "" : ",", analog->current_mv[i])) return false;
    }
    if (!append(output, output_size, &used, "],\"t_mv\":[")) return false;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (!append(output, output_size, &used, "%s%d",
                    i == 0U ? "" : ",", analog->temperature_mv[i])) return false;
    }
    return append(output, output_size, &used, "]}}");
}
