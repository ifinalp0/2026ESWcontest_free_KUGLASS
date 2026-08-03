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

}  // namespace

bool format_status_line(uint32_t seq,
                        const ChannelManager& channels,
                        const FaultManager& fault,
                        bool estop_active,
                        char* output,
                        size_t output_size) {
    if (output == nullptr || output_size == 0U) return false;
    output[0] = '\0';
    size_t used = 0;
    if (!append(output, output_size, &used,
                "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\","
                "\"seq\":%lu,\"estop\":%s,\"fault_code\":\"%s\",\"ch\":[",
                static_cast<unsigned long>(seq),
                estop_active ? "true" : "false", fault.code_name())) return false;
    for (size_t i = 0; i < channels.count(); ++i) {
        const ChannelRuntime* item = channels.channel(i);
        if (item == nullptr) return false;
        if (!append(output, output_size, &used,
                    "%s{\"id\":%u,\"mi\":%.4f,\"fault\":%s}",
                    i == 0U ? "" : ",", static_cast<unsigned>(item->channel_id),
                    item->applied_mi,
                    (fault.faulted() || item->faulted) ? "true" : "false")) return false;
    }
    return append(output, output_size, &used, "]}");
}
