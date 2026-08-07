#include "analog_monitor.h"
#include "channel_manager.h"
#include "control_protocol.h"
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
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "xt_utils.h"

#ifdef CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
#error "USB Serial/JTAG must be disabled because GPIO19 is EN_GLOBAL."
#endif
#ifndef CONFIG_ESP_CONSOLE_NONE
#error "ESP32_B runtime console must be disabled on the GPIO43/44 data link."
#endif
#ifndef CONFIG_BOOTLOADER_LOG_LEVEL_NONE
#error "ESP32_B second-stage bootloader logs must be disabled on GPIO43."
#endif
#ifndef CONFIG_BOOT_ROM_LOG_ALWAYS_ON
#error "Boot ROM logging must keep its default eFuse state; ESP32_A filters the known ROM boot lines."
#endif
#ifndef CONFIG_GPIO_CTRL_FUNC_IN_IRAM
#error "Safety GPIO ISR requires CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y."
#endif
#ifndef CONFIG_ESP_TASK_WDT_INIT
#error "The output safety task requires the ESP task watchdog."
#endif
#endif

namespace {

ChannelManager g_channels;
FaultManager g_fault;
[[maybe_unused]] SpwmGenerator g_spwm;
AnalogMonitor g_analog;

#ifdef ESP_PLATFORM
constexpr uart_port_t kUpstreamUart = UART_NUM_1;
constexpr int kUpstreamBaud = 115200;
constexpr EventBits_t kTasksMayStart = BIT0;
constexpr uintptr_t kEstopEventIndex = KUGLASS_CHANNEL_COUNT;

SemaphoreHandle_t g_state_mutex = nullptr;
SemaphoreHandle_t g_uart_mutex = nullptr;
SemaphoreHandle_t g_analog_mutex = nullptr;
SemaphoreHandle_t g_status_tx_mutex = nullptr;
EventGroupHandle_t g_task_start_event = nullptr;
uint32_t g_last_seq = 0;
uint32_t g_last_command_ms = 0;
uint32_t g_last_ttl_ms = KUGLASS_DEFAULT_TTL_MS;
bool g_has_command = false;
uint32_t g_status_seq = 0;
uint32_t g_boot_id = 0;

struct SafetyEventCounters {
    uint32_t estop = 0;
    uint32_t channel[KUGLASS_CHANNEL_COUNT] = {};
};

DRAM_ATTR volatile uint32_t g_estop_event_count = 0;
DRAM_ATTR volatile uint32_t
    g_channel_event_count[KUGLASS_CHANNEL_COUNT] = {};
DRAM_ATTR int g_enable_gpios[KUGLASS_CHANNEL_COUNT] = {};
DRAM_ATTR volatile bool g_safety_outputs_ready = false;
DRAM_ATTR volatile bool g_safety_trip_latched = false;
DRAM_ATTR volatile uint32_t g_safety_trip_epoch = 0;
DRAM_ATTR volatile uint32_t g_safety_trip_inflight = 0;
DRAM_ATTR volatile uint32_t g_reset_challenge = 0;
portMUX_TYPE g_safety_event_mux = portMUX_INITIALIZER_UNLOCKED;
SafetyEventCounters g_acknowledged_events;
uint32_t g_acknowledged_trip_epoch = 0;

uint32_t millis_now() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void wait_for_task_start() {
    xEventGroupWaitBits(g_task_start_event, kTasksMayStart,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

void IRAM_ATTR disable_all_enables_from_isr() {
    if (!g_safety_outputs_ready) return;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        (void)gpio_set_level(static_cast<gpio_num_t>(g_enable_gpios[i]), 0);
    }
}

uint32_t IRAM_ATTR atomic_add_safety_counter(volatile uint32_t* value,
                                             int32_t delta) {
    uint32_t before = 0;
    uint32_t after = 0;
    do {
        before = *value;
        after = static_cast<uint32_t>(before + static_cast<uint32_t>(delta));
    } while (!xt_utils_compare_and_set(value, before, after));
    __asm__ __volatile__("memw" ::: "memory");
    return after;
}

uint32_t IRAM_ATTR rotate_reset_challenge() {
    uint32_t challenge = atomic_add_safety_counter(&g_reset_challenge, 1);
    if (challenge == 0U) {
        challenge = atomic_add_safety_counter(&g_reset_challenge, 1);
    }
    return challenge;
}

bool consume_reset_challenge(uint32_t expected) {
    if (expected == 0U || g_reset_challenge != expected) return false;
    uint32_t replacement = expected + 1U;
    if (replacement == 0U) replacement = 1U;
    const bool consumed =
        xt_utils_compare_and_set(&g_reset_challenge, expected, replacement);
    __asm__ __volatile__("memw" ::: "memory");
    return consumed;
}

void IRAM_ATTR begin_safety_trip_from_isr() {
    // Publish an in-flight marker and a monotonic epoch before waiting for any
    // mux. Reset and ENABLE commit use both values as a lock-free seqlock.
    (void)atomic_add_safety_counter(&g_safety_trip_inflight, 1);
    (void)atomic_add_safety_counter(&g_safety_trip_epoch, 1);
    (void)rotate_reset_challenge();
    g_safety_trip_latched = true;
    __asm__ __volatile__("memw" ::: "memory");
    disable_all_enables_from_isr();
}

void IRAM_ATTR finish_safety_trip_from_isr() {
    (void)atomic_add_safety_counter(&g_safety_trip_inflight, -1);
}

void IRAM_ATTR latch_trip_and_disable_from_isr() {
    begin_safety_trip_from_isr();
    finish_safety_trip_from_isr();
}

void IRAM_ATTR safety_falling_edge_isr(void* argument) {
    begin_safety_trip_from_isr();
    const uintptr_t event_index = reinterpret_cast<uintptr_t>(argument);
    portENTER_CRITICAL_ISR(&g_safety_event_mux);
    if (event_index == kEstopEventIndex) {
        g_estop_event_count = g_estop_event_count + 1U;
    } else if (event_index < KUGLASS_CHANNEL_COUNT) {
        g_channel_event_count[event_index] =
            g_channel_event_count[event_index] + 1U;
    }
    // Cut again before releasing the lock. This also covers a commit that
    // started immediately before the lock-free trip became visible.
    disable_all_enables_from_isr();
    portEXIT_CRITICAL_ISR(&g_safety_event_mux);
    finish_safety_trip_from_isr();
}

SafetyEventCounters safety_event_snapshot() {
    SafetyEventCounters result;
    portENTER_CRITICAL(&g_safety_event_mux);
    result.estop = g_estop_event_count;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        result.channel[i] = g_channel_event_count[i];
    }
    portEXIT_CRITICAL(&g_safety_event_mux);
    return result;
}

bool safety_events_equal(const SafetyEventCounters& left,
                         const SafetyEventCounters& right) {
    if (left.estop != right.estop) return false;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (left.channel[i] != right.channel[i]) return false;
    }
    return true;
}

bool safety_events_pending(const SafetyEventCounters& events) {
    if (g_safety_trip_latched || g_safety_trip_inflight != 0U ||
        g_safety_trip_epoch != g_acknowledged_trip_epoch) {
        return true;
    }
    if (events.estop != g_acknowledged_events.estop) return true;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (events.channel[i] != g_acknowledged_events.channel[i]) return true;
    }
    return false;
}

bool commit_channel_enable_if_safe(size_t channel_index, void*) {
    if (channel_index >= KUGLASS_CHANNEL_COUNT || !g_safety_outputs_ready) {
        return false;
    }

    bool safe = false;
    portENTER_CRITICAL(&g_safety_event_mux);
    const uint32_t epoch_before = g_safety_trip_epoch;
    safe = !g_safety_trip_latched && g_safety_trip_inflight == 0U &&
        epoch_before == g_acknowledged_trip_epoch &&
        g_estop_event_count == g_acknowledged_events.estop &&
        gpio_get_level(static_cast<gpio_num_t>(
            KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) != 0;
    for (size_t i = 0; safe && i < KUGLASS_CHANNEL_COUNT; ++i) {
        safe = g_channel_event_count[i] ==
                   g_acknowledged_events.channel[i] &&
            gpio_get_level(static_cast<gpio_num_t>(
                KUGLASS_POWER_STAGE_PINMAP.channels[i].fault_n_gpio)) != 0;
    }
    if (safe) {
        safe = gpio_set_level(static_cast<gpio_num_t>(
            KUGLASS_POWER_STAGE_PINMAP.channels[channel_index].enable_gpio),
            1) == ESP_OK;
        __asm__ __volatile__("memw" ::: "memory");
        // The ISR can publish a trip and cut GPIO without taking this mux.
        // Checking after the HIGH write closes the ISR-before-commit race.
        if (g_safety_trip_latched || g_safety_trip_inflight != 0U ||
            g_safety_trip_epoch != epoch_before ||
            g_safety_trip_epoch != g_acknowledged_trip_epoch ||
            g_estop_event_count != g_acknowledged_events.estop) {
            safe = false;
        }
        for (size_t i = 0; safe && i < KUGLASS_CHANNEL_COUNT; ++i) {
            safe = g_channel_event_count[i] ==
                       g_acknowledged_events.channel[i] &&
                gpio_get_level(static_cast<gpio_num_t>(
                    KUGLASS_POWER_STAGE_PINMAP.channels[i].fault_n_gpio)) != 0;
        }
        if (safe) {
            safe = gpio_get_level(static_cast<gpio_num_t>(
                KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) != 0;
        }
        if (!safe) disable_all_enables_from_isr();
    }
    portEXIT_CRITICAL(&g_safety_event_mux);
    return safe;
}

bool physical_safety_inputs_high() {
    if (gpio_get_level(static_cast<gpio_num_t>(
            KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) == 0) {
        return false;
    }
    for (const PowerStageChannelPins& channel :
         KUGLASS_POWER_STAGE_PINMAP.channels) {
        if (gpio_get_level(static_cast<gpio_num_t>(channel.fault_n_gpio)) == 0) {
            return false;
        }
    }
    return true;
}

bool acknowledge_safety_events_if_inputs_high() {
    // Capture both the lock-free epoch and the per-input counters. A reset is
    // valid only when no ISR is in flight and the epoch remains stable through
    // the complete acknowledgement transaction.
    if (g_safety_trip_inflight != 0U) return false;
    const uint32_t candidate_epoch = g_safety_trip_epoch;
    __asm__ __volatile__("memw" ::: "memory");
    if (g_safety_trip_inflight != 0U) return false;
    const SafetyEventCounters candidate = safety_event_snapshot();
    if (!physical_safety_inputs_high()) return false;
    portENTER_CRITICAL(&g_safety_event_mux);
    bool accepted = g_safety_trip_inflight == 0U &&
        g_safety_trip_epoch == candidate_epoch &&
        g_estop_event_count == candidate.estop &&
        physical_safety_inputs_high();
    for (size_t i = 0; accepted && i < KUGLASS_CHANNEL_COUNT; ++i) {
        accepted = g_channel_event_count[i] == candidate.channel[i];
    }
    if (accepted) {
        g_acknowledged_events = candidate;
        g_acknowledged_trip_epoch = candidate_epoch;
        g_safety_trip_latched = false;
        __asm__ __volatile__("memw" ::: "memory");
        accepted = g_safety_trip_inflight == 0U &&
            g_safety_trip_epoch == candidate_epoch &&
            !g_safety_trip_latched && physical_safety_inputs_high();
    }
    if (!accepted) g_safety_trip_latched = true;
    portEXIT_CRITICAL(&g_safety_event_mux);
    return accepted;
}

bool inputs_ok(bool* estop_ok) {
    if (estop_ok == nullptr) return false;
    const SafetyEventCounters events = safety_event_snapshot();
    *estop_ok =
        gpio_get_level(static_cast<gpio_num_t>(
            KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) != 0 &&
        events.estop == g_acknowledged_events.estop;
    bool stages_ok = true;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const bool channel_ok =
            gpio_get_level(static_cast<gpio_num_t>(
                KUGLASS_POWER_STAGE_PINMAP.channels[i].fault_n_gpio)) != 0 &&
            events.channel[i] == g_acknowledged_events.channel[i];
        g_channels.set_fault(i, !channel_ok);
        stages_ok = stages_ok && channel_ok;
    }
    return stages_ok;
}

bool estop_is_active() {
    const SafetyEventCounters events = safety_event_snapshot();
    return gpio_get_level(static_cast<gpio_num_t>(
               KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio)) == 0 ||
           events.estop != g_acknowledged_events.estop;
}

void uart_write_line(const char* line) {
    if (line == nullptr || g_uart_mutex == nullptr) return;
    xSemaphoreTake(g_uart_mutex, portMAX_DELAY);
    uart_write_bytes(kUpstreamUart, line, std::strlen(line));
    uart_write_bytes(kUpstreamUart, "\n", 1);
    xSemaphoreGive(g_uart_mutex);
}

StatusMetadata status_metadata(const ResetControlResult* control_result = nullptr) {
    StatusMetadata metadata;
    metadata.boot_id = g_boot_id;
    metadata.reset_challenge = g_reset_challenge;
    metadata.control_result = control_result;
    return metadata;
}

void send_status_diagnostic(
        const char* diagnostic,
        const ResetControlResult* control_result = nullptr) {
    if (g_state_mutex == nullptr || g_analog_mutex == nullptr ||
        g_status_tx_mutex == nullptr) {
        return;
    }
    xSemaphoreTake(g_status_tx_mutex, portMAX_DELAY);
    const uint32_t now_ms = millis_now();
    xSemaphoreTake(g_analog_mutex, portMAX_DELAY);
    const AnalogTelemetrySnapshot analog = g_analog.snapshot(now_ms);
    xSemaphoreGive(g_analog_mutex);

    char line[1024];
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    const uint32_t status_seq = g_status_seq++;
    const StatusMetadata metadata = status_metadata(control_result);
    const bool formatted = format_status_line(
        status_seq, g_channels, g_fault, estop_is_active(), metadata,
        &analog, diagnostic, line, sizeof(line));
    xSemaphoreGive(g_state_mutex);
    if (formatted) uart_write_line(line);
    xSemaphoreGive(g_status_tx_mutex);
}

void send_protocol_error_name(const char* error_name) {
    send_status_diagnostic(error_name == nullptr ? "UNKNOWN" : error_name);
}

void send_protocol_error(ProtocolError error) {
    send_protocol_error_name(protocol_error_name(error));
}

void reject_active_command_locked() {
    g_fault.reject_command();
    g_channels.update(0.0f, false);
    g_spwm.force_safe();
}

void reject_active_command() {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    reject_active_command_locked();
    xSemaphoreGive(g_state_mutex);
}

bool handle_reset_fault_line(const char* line) {
    ResetFaultCommand reset;
    ControlProtocolError error;
    if (!parse_reset_fault_line(line, &reset, &error)) return false;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_spwm.force_safe();
    g_channels.update(0.0f, false);
    const char* result_error = "NONE";
    bool cleared = false;
    if (reset.target_boot_id != g_boot_id) {
        result_error = "TARGET_BOOT_MISMATCH";
    } else if (!consume_reset_challenge(reset.reset_challenge)) {
        result_error = "CHALLENGE_MISMATCH";
    } else if (acknowledge_safety_events_if_inputs_high()) {
        bool estop_ok = false;
        const bool stages_ok = inputs_ok(&estop_ok);
        cleared = g_fault.clear_if_safe(estop_ok, stages_ok);
        if (cleared) {
            g_channels.clear_faults();
            g_has_command = false;
            g_last_command_ms = 0;
            g_last_ttl_ms = KUGLASS_DEFAULT_TTL_MS;
        }
    }
    if (!cleared && std::strcmp(result_error, "NONE") == 0) {
        result_error = "RESET_UNSAFE";
    }
    xSemaphoreGive(g_state_mutex);

    const ResetControlResult result = {
        reset.seq,
        reset.source_session_id,
        cleared,
        result_error,
    };
    send_status_diagnostic(cleared ? "RESET_OK" : result_error, &result);
    return true;
}

void process_received_line(const char* line) {
    if (handle_reset_fault_line(line)) return;

    ProtocolCommand command;
    ProtocolError error;
    if (!parse_command_line(line, &command, &error)) {
        reject_active_command();
        send_protocol_error(error);
        return;
    }

    const uint32_t now_ms = millis_now();
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    const bool lease_active = g_has_command &&
        static_cast<uint32_t>(now_ms - g_last_command_ms) <= g_last_ttl_ms;
    const bool sequence_forward = !g_has_command ||
        static_cast<int32_t>(command.seq - g_last_seq) > 0;
    if (!sequence_forward && lease_active) {
        reject_active_command_locked();
        xSemaphoreGive(g_state_mutex);
        send_protocol_error_name("STALE_COMMAND_SEQUENCE");
        return;
    }
    if (!g_channels.apply_command(command)) {
        reject_active_command_locked();
        xSemaphoreGive(g_state_mutex);
        send_protocol_error_name("INVALID_CHANNEL_SET");
        return;
    }
    g_fault.note_command(now_ms, command.ttl_ms);
    g_last_seq = command.seq;
    g_last_command_ms = now_ms;
    g_last_ttl_ms = command.ttl_ms;
    g_has_command = true;
    xSemaphoreGive(g_state_mutex);
}

void rx_task(void*) {
    wait_for_task_start();
    JsonLineAccumulator accumulator;
    char line[512];
    while (true) {
        uint8_t byte = 0;
        if (uart_read_bytes(kUpstreamUart, &byte, 1,
                            pdMS_TO_TICKS(20)) <= 0) {
            continue;
        }
        const JsonLineResult result = accumulator.feed(byte, line, sizeof(line));
        if (result == JsonLineResult::OVERSIZE_DROPPED ||
            result == JsonLineResult::OUTPUT_TOO_SMALL) {
            reject_active_command();
            send_protocol_error_name("LINE_TOO_LARGE");
            continue;
        }
        if (result == JsonLineResult::COMPLETE) process_received_line(line);
    }
}

void output_task(void*) {
    wait_for_task_start();
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_spwm.force_safe();
        xSemaphoreGive(g_state_mutex);
        vTaskDelete(nullptr);
        return;
    }

    TickType_t next_wake = xTaskGetTickCount();
    int64_t previous_us = esp_timer_get_time();
    while (true) {
        const int64_t now_us = esp_timer_get_time();
        float dt_s = static_cast<float>(now_us - previous_us) / 1000000.0f;
        previous_us = now_us;
        if (dt_s <= 0.0f) {
            dt_s = static_cast<float>(KUGLASS_OUTPUT_UPDATE_MS) / 1000.0f;
        }
        const uint32_t now_ms = static_cast<uint32_t>(now_us / 1000ULL);

        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        bool estop_ok = false;
        const bool stages_ok = inputs_ok(&estop_ok);
        g_fault.update(now_ms, estop_ok, stages_ok);
        g_channels.update(dt_s, !g_fault.faulted());

        const SafetyEventCounters before_output = safety_event_snapshot();
        const bool pending_before_output =
            safety_events_pending(before_output) || !stages_ok || !estop_ok;
        if (pending_before_output) {
            g_channels.update(0.0f, false);
            g_spwm.force_safe();
        } else {
            g_spwm.tick(g_channels, dt_s, !g_fault.faulted());
            // If an ISR ran while outputs were being updated, the ISR already
            // lowered ENABLE. Force PWM low as well before leaving this cycle.
            if (!safety_events_equal(before_output,
                                     safety_event_snapshot())) {
                bool latest_estop_ok = false;
                const bool latest_stages_ok = inputs_ok(&latest_estop_ok);
                g_fault.update(now_ms, latest_estop_ok, latest_stages_ok);
                g_channels.update(0.0f, false);
                g_spwm.force_safe();
            }
        }
        xSemaphoreGive(g_state_mutex);

        if (esp_task_wdt_reset() != ESP_OK) {
            latch_trip_and_disable_from_isr();
        }
        vTaskDelayUntil(&next_wake,
                        pdMS_TO_TICKS(KUGLASS_OUTPUT_UPDATE_MS));
    }
}

void analog_task(void*) {
    wait_for_task_start();
    TickType_t next_wake = xTaskGetTickCount();
    while (true) {
        xSemaphoreTake(g_analog_mutex, portMAX_DELAY);
        if (g_analog.initialized()) (void)g_analog.sample(millis_now());
        xSemaphoreGive(g_analog_mutex);
        vTaskDelayUntil(&next_wake,
                        pdMS_TO_TICKS(KUGLASS_ANALOG_SCAN_PERIOD_MS));
    }
}

void status_task(void*) {
    wait_for_task_start();
    char line[1024];
    while (true) {
        xSemaphoreTake(g_status_tx_mutex, portMAX_DELAY);
        const uint32_t now_ms = millis_now();
        xSemaphoreTake(g_analog_mutex, portMAX_DELAY);
        const AnalogTelemetrySnapshot analog = g_analog.snapshot(now_ms);
        xSemaphoreGive(g_analog_mutex);

        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        const uint32_t status_seq = g_status_seq++;
        const StatusMetadata metadata = status_metadata();
        const bool formatted = format_status_line(
            status_seq, g_channels, g_fault, estop_is_active(), metadata,
            &analog, line, sizeof(line));
        xSemaphoreGive(g_state_mutex);
        if (formatted) uart_write_line(line);
        xSemaphoreGive(g_status_tx_mutex);
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
           uart_driver_install(kUpstreamUart, 2048, 2048, 0, nullptr, 0) ==
               ESP_OK;
}

bool configure_safety_inputs() {
    gpio_config_t estop_config = {};
    estop_config.pin_bit_mask =
        1ULL << KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio;
    estop_config.mode = GPIO_MODE_INPUT;
    estop_config.pull_up_en = GPIO_PULLUP_DISABLE;
    estop_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    estop_config.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&estop_config) != ESP_OK) return false;

    uint64_t fault_mask = 0;
    for (const PowerStageChannelPins& channel :
         KUGLASS_POWER_STAGE_PINMAP.channels) {
        fault_mask |= 1ULL << channel.fault_n_gpio;
    }
    gpio_config_t fault_config = {};
    fault_config.pin_bit_mask = fault_mask;
    fault_config.mode = GPIO_MODE_INPUT;
    fault_config.pull_up_en = GPIO_PULLUP_DISABLE;
    fault_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    fault_config.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&fault_config) != ESP_OK) return false;

    if (gpio_install_isr_service(ESP_INTR_FLAG_IRAM) != ESP_OK) return false;
    if (gpio_isr_handler_add(
            static_cast<gpio_num_t>(KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio),
            safety_falling_edge_isr,
            reinterpret_cast<void*>(kEstopEventIndex)) != ESP_OK) {
        return false;
    }
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        if (gpio_isr_handler_add(
                static_cast<gpio_num_t>(
                    KUGLASS_POWER_STAGE_PINMAP.channels[i].fault_n_gpio),
                safety_falling_edge_isr,
                reinterpret_cast<void*>(static_cast<uintptr_t>(i))) != ESP_OK) {
            return false;
        }
    }
    return true;
}

bool configure_output_watchdog() {
    esp_task_wdt_config_t config = {};
    config.timeout_ms = KUGLASS_SAFETY_WATCHDOG_MS;
    config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    config.trigger_panic = true;
    return esp_task_wdt_reconfigure(&config) == ESP_OK;
}

bool create_runtime_tasks() {
    return xTaskCreate(rx_task, "a_rx", 4096, nullptr, 8, nullptr) == pdPASS &&
           xTaskCreate(analog_task, "analog", 4096, nullptr, 4, nullptr) ==
               pdPASS &&
           xTaskCreate(status_task, "status_tx", 6144, nullptr, 5, nullptr) ==
               pdPASS &&
           xTaskCreate(output_task, "spwm", 4096, nullptr, 9, nullptr) ==
               pdPASS;
}
#endif

}  // namespace

#ifdef ESP_PLATFORM
extern "C" void IRAM_ATTR esp_task_wdt_isr_user_handler(void) {
    latch_trip_and_disable_from_isr();
}
#endif

extern "C" void app_main(void) {
    g_channels.begin();
    g_fault.begin();

#ifdef ESP_PLATFORM
    // This is intentionally the first peripheral action: make all software
    // enables low and hold every PWM output low before allocation/comms/tasks.
    g_spwm.begin(KUGLASS_POWER_STAGE_PINMAP);
    do {
        g_boot_id = esp_random();
    } while (g_boot_id == 0U);
    do {
        g_reset_challenge = esp_random();
    } while (g_reset_challenge == 0U);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        g_enable_gpios[i] = KUGLASS_POWER_STAGE_PINMAP.channels[i].enable_gpio;
    }
    g_safety_outputs_ready = true;
    g_spwm.set_enable_commit_callback(commit_channel_enable_if_safe, nullptr);

    if (!configure_safety_inputs()) {
        g_spwm.force_safe();
        return;
    }
    (void)g_analog.begin(KUGLASS_POWER_STAGE_PINMAP);
    if (!begin_uart()) {
        g_spwm.force_safe();
        return;
    }

    g_state_mutex = xSemaphoreCreateMutex();
    g_uart_mutex = xSemaphoreCreateMutex();
    g_analog_mutex = xSemaphoreCreateMutex();
    g_status_tx_mutex = xSemaphoreCreateMutex();
    g_task_start_event = xEventGroupCreate();
    if (g_state_mutex == nullptr || g_uart_mutex == nullptr ||
        g_analog_mutex == nullptr || g_status_tx_mutex == nullptr ||
        g_task_start_event == nullptr ||
        !configure_output_watchdog()) {
        g_spwm.force_safe();
        return;
    }

    send_status_diagnostic(g_analog.initialized()
        ? "BOOT" : "BOOT_ADC_UNAVAILABLE");

    if (!create_runtime_tasks()) {
        g_spwm.force_safe();
        send_protocol_error_name("TASK_CREATE_FAILED");
        return;
    }
    xEventGroupSetBits(g_task_start_event, kTasksMayStart);
#endif
}
