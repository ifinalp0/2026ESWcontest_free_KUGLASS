#include "camera_metric_adapter.h"
#include "downstream_status.h"
#include "ds18b20_sensor.h"
#include "esp32_b_link.h"
#include "kuglass_config.h"
#include "master_telemetry.h"
#include "policy_engine.h"
#include "server_console.h"
#include "ui_protocol.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace {

PolicyEngine g_policy;
[[maybe_unused]] CameraMetricAdapter g_camera;
[[maybe_unused]] Ds18b20Sensor g_temperature(KUGLASS_DS18B20_GPIO);
[[maybe_unused]] Esp32BLink g_downstream;

#ifdef ESP_PLATFORM
QueueHandle_t g_command_queue = nullptr;
SemaphoreHandle_t g_state_mutex = nullptr;
SensorSnapshot g_physical_sensors;
SensorSnapshot g_effective_sensors;
PolicyDecision g_last_decision;
bool g_decision_valid = false;

uint32_t millis_now() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

SensorSnapshot copy_physical_sensors() {
    SensorSnapshot copy;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    copy = g_physical_sensors;
    xSemaphoreGive(g_state_mutex);
    return copy;
}

void publish_sensor_snapshot(const SensorSnapshot& snapshot) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_physical_sensors = snapshot;
    xSemaphoreGive(g_state_mutex);
}

void publish_decision(const PolicyDecision& decision,
                      const SensorSnapshot& effective_sensors) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_last_decision = decision;
    g_effective_sensors = effective_sensors;
    g_decision_valid = true;
    xSemaphoreGive(g_state_mutex);
}

void ui_rx_task(void*) {
    char line[1024];
    while (true) {
        if (std::fgets(line, sizeof(line), stdin) == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const size_t length = std::strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int character = 0;
            do {
                character = std::fgetc(stdin);
            } while (character != '\n' && character != EOF);
            server_console_send_protocol_error("tabui", "LINE_TOO_LARGE");
            continue;
        }

        UiCommand command;
        UiProtocolError error;
        if (!parse_ui_command_line(line, &command, &error)) {
            server_console_send_protocol_error("tabui", ui_protocol_error_name(error));
            continue;
        }
        if (xQueueSend(g_command_queue, &command, 0) != pdTRUE) {
            server_console_send_ack(command, false, "COMMAND_QUEUE_FULL");
        }
    }
}

void sensor_task(void*) {
    SensorSnapshot snapshot;
    const bool camera_started = g_camera.begin();
    const bool temperature_started = g_temperature.begin();
    char status[192];
    std::snprintf(status, sizeof(status),
                  "{\"type\":\"sensor\",\"camera_available\":%s,"
                  "\"temperature_available\":%s}",
                  camera_started ? "true" : "false",
                  temperature_started ? "true" : "false");
    server_console_send_line(status);

    uint32_t last_camera_sample_ms = 0;
    while (true) {
        const uint32_t now_ms = millis_now();
        if (g_camera.available() &&
            static_cast<uint32_t>(now_ms - last_camera_sample_ms) >= 100U) {
            g_camera.sample(now_ms, &snapshot);
            last_camera_sample_ms = now_ms;
        }
        float temperature_c = 0.0f;
        if (g_temperature.poll(now_ms, &temperature_c)) {
            snapshot.internal_temp_c = temperature_c;
            snapshot.internal_temp_valid = true;
            snapshot.internal_temp_timestamp_ms = now_ms;
        }
        if (snapshot.camera_valid &&
            !timestamp_fresh(now_ms, snapshot.camera_timestamp_ms, KUGLASS_CAMERA_STALE_MS)) {
            snapshot.camera_valid = false;
        }
        if (snapshot.internal_temp_valid &&
            !timestamp_fresh(now_ms, snapshot.internal_temp_timestamp_ms,
                             KUGLASS_TEMPERATURE_STALE_MS)) {
            snapshot.internal_temp_valid = false;
        }
        publish_sensor_snapshot(snapshot);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void control_task(void*) {
    TickType_t next_wake = xTaskGetTickCount();
    while (true) {
        const uint32_t now_ms = millis_now();
        UiCommand command;
        while (xQueueReceive(g_command_queue, &command, 0) == pdTRUE) {
            PolicyApplyResult result = g_policy.apply_command(command, now_ms);
            if (result.accepted && command.type == UiCommandType::RESET_FAULT) {
#if KUGLASS_B_SUPPORTS_RESET_FAULT
                if (!g_downstream.send_reset_fault(command.seq)) {
                    result = {false, g_downstream.error_name()};
                }
#else
                result = {false, "B_RESET_UNSUPPORTED"};
#endif
            }
            server_console_send_ack(command, result.accepted, result.error);
        }

        const SensorSnapshot physical = copy_physical_sensors();
        const PolicyDecision decision = g_policy.update(physical, now_ms);
        const SensorSnapshot effective = g_policy.effective_sensors(physical, now_ms);
        g_downstream.send_decision(decision);
        publish_decision(decision, effective);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(KUGLASS_CONTROL_PERIOD_MS));
    }
}

void telemetry_task(void*) {
    char line[4096];
    while (true) {
        PolicyDecision decision;
        SensorSnapshot sensors;
        bool valid = false;
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        decision = g_last_decision;
        sensors = g_effective_sensors;
        valid = g_decision_valid;
        xSemaphoreGive(g_state_mutex);

        const uint32_t now_ms = millis_now();
        if (valid && format_master_state(decision,
                                         sensors,
                                         now_ms,
                                         g_downstream.status_healthy(now_ms),
                                         g_downstream.status_name(now_ms),
                                         line,
                                         sizeof(line))) {
            server_console_send_line(line);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void downstream_rx_task(void*) {
    char line[1024];
    while (true) {
        if (g_downstream.read_line(line, sizeof(line), 20)) {
            DownstreamStatus status;
            DownstreamStatusError error;
            if (parse_downstream_status_line(line, &status, &error)) {
                const uint32_t now_ms = millis_now();
                if (!g_downstream.note_valid_status(now_ms, status.seq)) {
                    server_console_send_protocol_error(
                        "esp32_b", "STALE_STATUS_SEQUENCE");
                    continue;
                }
                // ESP32_B status JSON is relayed directly so the TabUI backend
                // can merge actuator telemetry with A's policy state.
                server_console_send_line(line);
            } else {
                server_console_send_protocol_error(
                    "esp32_b", downstream_status_error_name(error));
            }
        } else {
            // read_line already waits when UART1 is active; this also prevents
            // a tight loop when initialization failed or no B is connected.
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}
#endif

}  // namespace

extern "C" void app_main(void) {
    g_policy.begin();
    server_console_begin();

#ifdef ESP_PLATFORM
    g_command_queue = xQueueCreate(16, sizeof(UiCommand));
    g_state_mutex = xSemaphoreCreateMutex();
    if (g_command_queue == nullptr || g_state_mutex == nullptr) {
        ESP_LOGE("kuglass_a", "Failed to allocate master queues");
        return;
    }

    const bool downstream_started = g_downstream.begin();
    char boot[256];
    std::snprintf(boot, sizeof(boot),
                  "{\"type\":\"boot\",\"controller_id\":\"A\","
                  "\"role\":\"algorithm_master\",\"diagnostics_enabled\":%s,"
                  "\"downstream_ready\":%s}",
                  KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS ? "true" : "false",
                  downstream_started ? "true" : "false");
    server_console_send_line(boot);

    xTaskCreate(ui_rx_task, "tabui_rx", 6144, nullptr, 7, nullptr);
    xTaskCreate(sensor_task, "sensors", 8192, nullptr, 6, nullptr);
    xTaskCreate(control_task, "policy_20hz", 6144, nullptr, 8, nullptr);
    xTaskCreate(telemetry_task, "state_tx", 6144, nullptr, 5, nullptr);
    xTaskCreate(downstream_rx_task, "esp32_b_rx", 4096, nullptr, 5, nullptr);
#endif
}
