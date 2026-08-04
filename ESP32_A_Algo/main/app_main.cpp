#include "camera_metric_adapter.h"
#include "camera_recovery.h"
#include "downstream_status.h"
#include "ds18b20_sensor.h"
#include "esp32_b_link.h"
#include "kuglass_config.h"
#include "master_telemetry.h"
#include "policy_engine.h"
#include "server_console.h"
#include "sensor_state.h"
#include "ui_protocol.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace {

PolicyEngine g_policy;
[[maybe_unused]] CameraMetricAdapter g_camera;
[[maybe_unused]] Ds18b20Sensor g_temperature(KUGLASS_DS18B20_GPIO);
[[maybe_unused]] Esp32BLink g_downstream;
[[maybe_unused]] ResetFaultCoordinator g_reset_fault;

#ifdef ESP_PLATFORM
QueueHandle_t g_command_queue = nullptr;
SemaphoreHandle_t g_state_mutex = nullptr;
EventGroupHandle_t g_task_start_event = nullptr;
SensorSnapshot g_physical_sensors;
SensorSnapshot g_effective_sensors;
PolicyDecision g_last_decision;
bool g_decision_valid = false;
bool g_camera_available = false;
bool g_temperature_available = false;
char g_telemetry_line[4096];
uint32_t g_source_session_id = 0;

constexpr EventBits_t kTaskStartBit = BIT0;

uint32_t millis_now() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t nonzero_random_id() {
    uint32_t value = 0;
    while (value == 0U) value = esp_random();
    return value;
}

void send_reset_outcome(const ResetFaultOutcome& outcome) {
    UiCommand command;
    command.type = UiCommandType::RESET_FAULT;
    command.has_seq = true;
    command.seq = outcome.seq;
    server_console_send_ack(command, outcome.ok, outcome.error);
}

void wait_for_task_start() {
    xEventGroupWaitBits(g_task_start_event, kTaskStartBit,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

void release_startup_objects() {
    if (g_task_start_event != nullptr) {
        vEventGroupDelete(g_task_start_event);
        g_task_start_event = nullptr;
    }
    if (g_state_mutex != nullptr) {
        vSemaphoreDelete(g_state_mutex);
        g_state_mutex = nullptr;
    }
    if (g_command_queue != nullptr) {
        vQueueDelete(g_command_queue);
        g_command_queue = nullptr;
    }
}

SensorSnapshot copy_physical_sensors(uint32_t now_ms) {
    SensorSnapshot copy;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    invalidate_stale_sensors(now_ms,
                             KUGLASS_CAMERA_STALE_MS,
                             KUGLASS_TEMPERATURE_STALE_MS,
                             &g_physical_sensors);
    copy = g_physical_sensors;
    xSemaphoreGive(g_state_mutex);
    return copy;
}

void publish_camera_sample(const SensorSnapshot& sample) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    merge_camera_sample(sample, &g_physical_sensors);
    xSemaphoreGive(g_state_mutex);
}

void invalidate_camera_sample() {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    invalidate_camera_sample_state(&g_physical_sensors);
    xSemaphoreGive(g_state_mutex);
}

void publish_temperature_sample(float temperature_c, uint32_t timestamp_ms) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    merge_temperature_sample(temperature_c, timestamp_ms, &g_physical_sensors);
    xSemaphoreGive(g_state_mutex);
}

void invalidate_temperature_sample() {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    invalidate_temperature_sample_state(&g_physical_sensors);
    xSemaphoreGive(g_state_mutex);
}

void report_sensor_availability(bool camera_sensor, bool available) {
    bool camera_available = false;
    bool temperature_available = false;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (camera_sensor) {
        g_camera_available = available;
    } else {
        g_temperature_available = available;
    }
    camera_available = g_camera_available;
    temperature_available = g_temperature_available;
    xSemaphoreGive(g_state_mutex);

    char status[192];
    std::snprintf(status, sizeof(status),
                  "{\"type\":\"sensor\",\"camera_available\":%s,"
                  "\"temperature_available\":%s}",
                  camera_available ? "true" : "false",
                  temperature_available ? "true" : "false");
    server_console_send_line(status);
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
    wait_for_task_start();
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

void camera_task(void*) {
    wait_for_task_start();
    CameraRecoveryState recovery(KUGLASS_CAMERA_RETRY_MS,
                                 KUGLASS_CAMERA_RETRY_MAX_MS,
                                 KUGLASS_CAMERA_FAILURES_BEFORE_RESTART);
    bool availability_reported = false;
    bool last_reported_availability = false;
    const auto report_if_changed = [&](bool available) {
        if (!availability_reported || available != last_reported_availability) {
            report_sensor_availability(true, available);
            availability_reported = true;
            last_reported_availability = available;
        }
    };

    while (true) {
        const uint32_t now_ms = millis_now();
        if (!recovery.running()) {
            if (recovery.start_due(now_ms)) {
                const bool started = g_camera.begin();
                const uint32_t completed_ms = millis_now();
                recovery.note_start_result(started, completed_ms);
                report_if_changed(started);
                if (!started) invalidate_camera_sample();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        SensorSnapshot sample;
        const bool captured = g_camera.sample(&sample);
        const uint32_t completed_ms = millis_now();
        const bool fresh_capture = captured &&
            timestamp_fresh(completed_ms, sample.camera_timestamp_ms,
                            KUGLASS_CAMERA_STALE_MS);
        if (fresh_capture) {
            publish_camera_sample(sample);
        } else {
            invalidate_camera_sample();
        }
        if (recovery.note_capture_result(captured, completed_ms)) {
            g_camera.stop();
            report_if_changed(false);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void temperature_task(void*) {
    wait_for_task_start();
    bool availability_reported = false;
    bool last_reported_availability = false;
    const auto report_if_changed = [&](bool available) {
        if (!availability_reported || available != last_reported_availability) {
            report_sensor_availability(false, available);
            availability_reported = true;
            last_reported_availability = available;
        }
    };

    report_if_changed(g_temperature.begin());
    while (true) {
        const uint32_t poll_started_ms = millis_now();
        float temperature_c = 0.0f;
        if (g_temperature.poll(poll_started_ms, &temperature_c)) {
            publish_temperature_sample(temperature_c, millis_now());
        }
        const bool available = g_temperature.available();
        report_if_changed(available);
        if (!available) {
            invalidate_temperature_sample();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void control_task(void*) {
    wait_for_task_start();
    TickType_t next_wake = xTaskGetTickCount();
    while (true) {
        const uint32_t now_ms = millis_now();
        ResetFaultOutcome timeout_outcome;
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        const bool reset_timed_out =
            g_reset_fault.poll_timeout(now_ms, &timeout_outcome);
        xSemaphoreGive(g_state_mutex);
        if (reset_timed_out) send_reset_outcome(timeout_outcome);

        UiCommand command;
        while (xQueueReceive(g_command_queue, &command, 0) == pdTRUE) {
            PolicyApplyResult result = g_policy.apply_command(command, now_ms);
            bool defer_ack = false;
            if (result.accepted && command.type == UiCommandType::RESET_FAULT) {
#if KUGLASS_B_SUPPORTS_RESET_FAULT
                ResetFaultRequest request;
                const char* reset_error = nullptr;
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                const bool started = g_reset_fault.begin(
                    command.seq, g_source_session_id, now_ms,
                    &request, &reset_error);
                xSemaphoreGive(g_state_mutex);
                if (!started) {
                    result = {false, reset_error};
                } else if (!g_downstream.send_reset_fault(request)) {
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    g_reset_fault.cancel(command.seq);
                    xSemaphoreGive(g_state_mutex);
                    result = {false, g_downstream.error_name()};
                } else {
                    // Final ACK is emitted only by an exactly correlated B
                    // control_result, or by the 1500 ms timeout above.
                    defer_ack = true;
                }
#else
                result = {false, "B_RESET_UNSUPPORTED"};
#endif
            }
            if (!defer_ack) {
                server_console_send_ack(command, result.accepted, result.error);
            }
        }

        const SensorSnapshot physical = copy_physical_sensors(now_ms);
        const PolicyDecision decision = g_policy.update(physical, now_ms);
        const SensorSnapshot effective = g_policy.effective_sensors(physical, now_ms);
        g_downstream.send_decision(decision);
        publish_decision(decision, effective);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(KUGLASS_CONTROL_PERIOD_MS));
    }
}

void telemetry_task(void*) {
    wait_for_task_start();
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
                                         g_telemetry_line,
                                         sizeof(g_telemetry_line))) {
            server_console_send_line(g_telemetry_line);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void downstream_rx_task(void*) {
    wait_for_task_start();
    char line[1024];
    while (true) {
        if (g_downstream.read_line(line, sizeof(line), 20)) {
            DownstreamStatus status;
            DownstreamStatusError error;
            if (parse_downstream_status_line(line, &status, &error)) {
                const uint32_t now_ms = millis_now();
                if (!g_downstream.note_valid_status(
                        now_ms, status.boot_id, status.seq)) {
                    server_console_send_protocol_error(
                        "esp32_b", "STALE_STATUS_SEQUENCE");
                    continue;
                }
                ResetFaultOutcome reset_outcome;
                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                g_reset_fault.note_status_context(status);
                const bool reset_completed =
                    g_reset_fault.note_control_result(status, &reset_outcome);
                xSemaphoreGive(g_state_mutex);
                // ESP32_B status JSON is relayed directly so the TabUI backend
                // can merge actuator telemetry with A's policy state.
                server_console_send_line(line);
                if (reset_completed) send_reset_outcome(reset_outcome);
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

#ifdef ESP_PLATFORM
    if (!server_console_begin()) {
        ESP_LOGE("kuglass_a", "Failed to allocate the console mutex");
        return;
    }
    g_command_queue = xQueueCreate(16, sizeof(UiCommand));
    g_state_mutex = xSemaphoreCreateMutex();
    g_task_start_event = xEventGroupCreate();
    if (g_command_queue == nullptr || g_state_mutex == nullptr ||
        g_task_start_event == nullptr) {
        ESP_LOGE("kuglass_a", "Failed to allocate master synchronization objects");
        release_startup_objects();
        return;
    }

    TaskHandle_t task_handles[6] = {};
    size_t task_count = 0;
    const auto create_task = [&](TaskFunction_t function,
                                 const char* name,
                                 uint32_t stack_size,
                                 UBaseType_t priority) {
        TaskHandle_t handle = nullptr;
        if (xTaskCreate(function, name, stack_size, nullptr, priority, &handle) != pdPASS) {
            ESP_LOGE("kuglass_a", "Failed to create task %s", name);
            return false;
        }
        task_handles[task_count++] = handle;
        return true;
    };

    const bool tasks_created =
        create_task(control_task, "policy_20hz", 6144, 8) &&
        create_task(ui_rx_task, "tabui_rx", 6144, 7) &&
        create_task(downstream_rx_task, "esp32_b_rx", 4096, 5) &&
        create_task(telemetry_task, "state_tx", 6144, 5) &&
        create_task(temperature_task, "temperature", 4096, 6) &&
        create_task(camera_task, "camera", 8192, 6);
    if (!tasks_created) {
        server_console_send_protocol_error("esp32_a", "TASK_CREATION_FAILED");
        for (size_t i = 0; i < task_count; ++i) {
            vTaskDelete(task_handles[i]);
        }
        release_startup_objects();
        return;
    }

    g_source_session_id = nonzero_random_id();
    const bool downstream_started = g_downstream.begin();
    char boot[256];
    std::snprintf(boot, sizeof(boot),
                  "{\"type\":\"boot\",\"controller_id\":\"A\","
                  "\"role\":\"algorithm_master\",\"diagnostics_enabled\":%s,"
                  "\"downstream_ready\":%s,\"source_session_id\":%lu}",
                  KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS ? "true" : "false",
                  downstream_started ? "true" : "false",
                  static_cast<unsigned long>(g_source_session_id));
    server_console_send_line(boot);
    xEventGroupSetBits(g_task_start_event, kTaskStartBit);
#endif
}
