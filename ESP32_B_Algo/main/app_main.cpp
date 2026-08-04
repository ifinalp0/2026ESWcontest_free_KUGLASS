#include "channel_manager.h"
#include "fault_manager.h"
#include "json_line_accumulator.h"
#include "kuglass_b_config.h"
#include "power_stage_pinmap.h"
#include "protocol.h"
#include "spwm_generator.h"
#include "status_reporter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace {

ChannelManager g_channels;
FaultManager g_fault;
[[maybe_unused]] SpwmGenerator g_spwm;

#ifdef ESP_PLATFORM
constexpr uart_port_t kUpstreamUart = UART_NUM_1;
constexpr int kUpstreamBaud = 115200;
SemaphoreHandle_t g_state_mutex = nullptr;
SemaphoreHandle_t g_uart_mutex = nullptr;
uint32_t g_last_seq = 0;
uint32_t g_last_command_ms = 0;
uint32_t g_last_ttl_ms = KUGLASS_DEFAULT_TTL_MS;
bool g_has_command = false;

uint32_t millis_now() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool inputs_ok(bool* estop_ok) {
    *estop_ok = gpio_get_level(
        static_cast<gpio_num_t>(KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) != 0;
    bool stages_ok = true;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const bool channel_ok = gpio_get_level(static_cast<gpio_num_t>(
            KUGLASS_POWER_STAGE_PINMAP.channels[i].fault_n_gpio)) != 0;
        g_channels.set_fault(i, !channel_ok);
        stages_ok = stages_ok && channel_ok;
    }
    return stages_ok;
}

void uart_write_line(const char* line) {
    if (line == nullptr) return;
    xSemaphoreTake(g_uart_mutex, portMAX_DELAY);
    uart_write_bytes(kUpstreamUart, line, std::strlen(line));
    uart_write_bytes(kUpstreamUart, "\n", 1);
    xSemaphoreGive(g_uart_mutex);
}

void send_protocol_error(ProtocolError error) {
    char line[128];
    std::snprintf(line, sizeof(line),
                  "{\"type\":\"protocol_error\",\"source\":\"esp32_b\","
                  "\"error\":\"%s\"}", protocol_error_name(error));
    uart_write_line(line);
}

void rx_task(void*) {
    JsonLineAccumulator accumulator;
    char line[512];
    while (true) {
        uint8_t byte = 0;
        if (uart_read_bytes(kUpstreamUart, &byte, 1, pdMS_TO_TICKS(20)) <= 0) continue;
        const JsonLineResult result = accumulator.feed(byte, line, sizeof(line));
        if (result == JsonLineResult::OVERSIZE_DROPPED ||
            result == JsonLineResult::OUTPUT_TOO_SMALL) {
            uart_write_line(
                "{\"type\":\"protocol_error\",\"source\":\"esp32_b\","
                "\"error\":\"LINE_TOO_LARGE\"}");
            continue;
        }
        if (result != JsonLineResult::COMPLETE) continue;

        ProtocolCommand command;
        ProtocolError error;
        if (!parse_command_line(line, &command, &error)) {
            send_protocol_error(error);
            continue;
        }

        const uint32_t now_ms = millis_now();
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        const bool lease_active = g_has_command &&
            static_cast<uint32_t>(now_ms - g_last_command_ms) <= g_last_ttl_ms;
        const bool sequence_forward = !g_has_command ||
            static_cast<int32_t>(command.seq - g_last_seq) > 0;
        const bool safe_rebase = !lease_active &&
            g_fault.code() == FaultCode::COMM_TIMEOUT;
        if (!sequence_forward && !safe_rebase) {
            xSemaphoreGive(g_state_mutex);
            uart_write_line(
                "{\"type\":\"protocol_error\",\"source\":\"esp32_b\","
                "\"error\":\"STALE_COMMAND_SEQUENCE\"}");
            continue;
        }
        g_channels.apply_command(command);
        g_fault.note_command(now_ms, command.ttl_ms);
        g_last_seq = command.seq;
        g_last_command_ms = now_ms;
        g_last_ttl_ms = command.ttl_ms;
        g_has_command = true;
        xSemaphoreGive(g_state_mutex);
    }
}

void output_task(void*) {
    TickType_t next_wake = xTaskGetTickCount();
    while (true) {
        const uint32_t now_ms = millis_now();
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        bool estop_ok = false;
        const bool stages_ok = inputs_ok(&estop_ok);
        g_fault.update(now_ms, estop_ok, stages_ok);
        g_channels.update(0.001f, !g_fault.faulted());
        g_spwm.tick(g_channels, 0.001f, !g_fault.faulted());
        xSemaphoreGive(g_state_mutex);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(1));
    }
}

void status_task(void*) {
    char line[512];
    while (true) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        const bool estop_active = gpio_get_level(static_cast<gpio_num_t>(
            KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) == 0;
        const bool formatted = format_status_line(
            g_last_seq, g_channels, g_fault, estop_active, line, sizeof(line));
        xSemaphoreGive(g_state_mutex);
        if (formatted) uart_write_line(line);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool begin_uart() {
    uart_config_t config = {};
    config.baud_rate = kUpstreamBaud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    return uart_param_config(kUpstreamUart, &config) == ESP_OK &&
           uart_set_pin(kUpstreamUart,
                        KUGLASS_A_UART_TX_GPIO,
                        KUGLASS_A_UART_RX_GPIO,
                        UART_PIN_NO_CHANGE,
                        UART_PIN_NO_CHANGE) == ESP_OK &&
           uart_driver_install(kUpstreamUart, 2048, 2048, 0, nullptr, 0) == ESP_OK;
}

void configure_safety_inputs() {
    gpio_config_t estop_config = {};
    estop_config.pin_bit_mask =
        1ULL << KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio;
    estop_config.mode = GPIO_MODE_INPUT;
    estop_config.pull_up_en = GPIO_PULLUP_DISABLE;
    estop_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    estop_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&estop_config));

    uint64_t fault_mask = 0;
    for (const PowerStageChannelPins& channel : KUGLASS_POWER_STAGE_PINMAP.channels) {
        fault_mask |= 1ULL << channel.fault_n_gpio;
    }
    gpio_config_t fault_config = {};
    fault_config.pin_bit_mask = fault_mask;
    fault_config.mode = GPIO_MODE_INPUT;
    fault_config.pull_up_en = GPIO_PULLUP_DISABLE;
    fault_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    fault_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&fault_config));
}
#endif

}  // namespace

extern "C" void app_main(void) {
    g_channels.begin();
    g_fault.begin();

#ifdef ESP_PLATFORM
    g_state_mutex = xSemaphoreCreateMutex();
    g_uart_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == nullptr || g_uart_mutex == nullptr) return;

    // Assert every software enable LOW before inputs, UART, or tasks start.
    g_spwm.begin(KUGLASS_POWER_STAGE_PINMAP);
    configure_safety_inputs();
    if (!begin_uart()) return;
    uart_write_line(
        "{\"type\":\"boot\",\"controller_id\":\"B\","
        "\"role\":\"four_channel_actuator\"}");
    xTaskCreate(rx_task, "a_rx", 4096, nullptr, 8, nullptr);
    xTaskCreate(output_task, "spwm", 4096, nullptr, 9, nullptr);
    xTaskCreate(status_task, "status_tx", 4096, nullptr, 5, nullptr);
#endif
}
