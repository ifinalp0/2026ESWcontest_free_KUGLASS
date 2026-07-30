#include "telemetry_reporter.h"

#include "kuglass_config.h"

#include <cstdio>

void TelemetryReporter::report_status(const char* controller_id, uint32_t seq, const ChannelManager& channels, const FaultManager& fault) {
    std::printf("{\"type\":\"status\",\"controller_id\":\"%s\",\"seq\":%lu,\"mode\":\"%s\",\"fault\":%s,\"code\":\"%s\",\"global_en\":%s,\"dc_v\":%.1f,\"ch\":[",
                controller_id,
                static_cast<unsigned long>(seq),
                fault.faulted() ? "FAULT" : "RUN",
                fault.faulted() ? "true" : "false",
                fault.code_name(),
                fault.faulted() ? "false" : "true",
                KUGLASS_DC_LINK_NOMINAL_V);
    for (size_t i = 0; i < channels.count(); ++i) {
        const ChannelRuntime* channel = channels.local(i);
        if (channel == nullptr) {
            continue;
        }
        if (i != 0) {
            std::printf(",");
        }
        std::printf("{\"id\":%u,\"mi\":%.4f,\"target_mi\":%.4f,\"enable\":%s,\"vrms\":%.2f,\"irms\":%.4f,\"temp_c\":%.1f,\"fault\":%s}",
                    channel->global_id,
                    channel->applied_mi,
                    channel->target_mi,
                    channel->enable ? "true" : "false",
                    channel->vrms,
                    channel->irms,
                    channel->temp_c,
                    channel->faulted ? "true" : "false");
    }
    std::printf("]}\n");
    std::fflush(stdout);
}

