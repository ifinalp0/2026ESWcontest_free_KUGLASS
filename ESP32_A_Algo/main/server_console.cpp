#include "server_console.h"

#include <cstdio>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
SemaphoreHandle_t g_console_mutex = nullptr;
}
#endif

void server_console_begin() {
#ifdef ESP_PLATFORM
    g_console_mutex = xSemaphoreCreateMutex();
#endif
}
void server_console_send_line(const char* line) {
    if (line == nullptr) return;
#ifdef ESP_PLATFORM
    if (g_console_mutex != nullptr) {
        xSemaphoreTake(g_console_mutex, portMAX_DELAY);
    }
#endif
    std::printf("%s\n", line);
    std::fflush(stdout);
#ifdef ESP_PLATFORM
    if (g_console_mutex != nullptr) {
        xSemaphoreGive(g_console_mutex);
    }
#endif
}

void server_console_send_ack(const UiCommand& command, bool ok, const char* error) {
    char line[256];
    const char* command_name = ui_command_type_name(command.type);
    if (command.has_seq) {
        if (ok) {
            std::snprintf(line, sizeof(line),
                          "{\"type\":\"ack\",\"seq\":%lu,\"command\":\"%s\",\"ok\":true}",
                          static_cast<unsigned long>(command.seq), command_name);
        } else {
            std::snprintf(line, sizeof(line),
                          "{\"type\":\"ack\",\"seq\":%lu,\"command\":\"%s\",\"ok\":false,\"error\":\"%s\"}",
                          static_cast<unsigned long>(command.seq), command_name,
                          error == nullptr ? "REJECTED" : error);
        }
    } else if (ok) {
        std::snprintf(line, sizeof(line),
                      "{\"type\":\"ack\",\"seq\":null,\"command\":\"%s\",\"ok\":true}",
                      command_name);
    } else {
        std::snprintf(line, sizeof(line),
                      "{\"type\":\"ack\",\"seq\":null,\"command\":\"%s\",\"ok\":false,\"error\":\"%s\"}",
                      command_name, error == nullptr ? "REJECTED" : error);
    }
    server_console_send_line(line);
}

void server_console_send_protocol_error(const char* source, const char* error) {
    char line[192];
    std::snprintf(line, sizeof(line),
                  "{\"type\":\"protocol_error\",\"source\":\"%s\",\"error\":\"%s\"}",
                  source == nullptr ? "unknown" : source,
                  error == nullptr ? "UNKNOWN" : error);
    server_console_send_line(line);
}
