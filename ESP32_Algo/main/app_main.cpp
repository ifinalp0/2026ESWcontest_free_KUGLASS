#include "adc_sampler.h"
#include "channel_manager.h"
#include "fault_manager.h"
#include "kuglass_config.h"
#include "pinmap.h"
#include "protocol.h"
#include "spwm_generator.h"
#include "telemetry_reporter.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {

ChannelManager g_channels;
FaultManager g_fault;
SpwmGenerator g_spwm;
AdcSampler g_adc;
TelemetryReporter g_telemetry;
volatile uint32_t g_last_seq = 0;

uint32_t millis_now() {
#ifdef ESP_PLATFORM
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#else
    return 0;
#endif
}

bool local_fault_pins_ok() {
#ifdef ESP_PLATFORM
    for (size_t i = 0; i < KUGLASS_LOCAL_CHANNELS; ++i) {
        const int pin = KUGLASS_PINMAP.channel[i].fault_n_gpio;
        if (gpio_get_level(static_cast<gpio_num_t>(pin)) == 0) {
            return false;
        }
    }
#endif
    return true;
}

bool estop_ok() {
#ifdef ESP_PLATFORM
    return gpio_get_level(static_cast<gpio_num_t>(KUGLASS_PINMAP.estop_n_gpio)) != 0;
#else
    return true;
#endif
}

#ifdef ESP_PLATFORM
void serial_rx_task(void*) {
    char line[256];
    while (true) {
        if (std::fgets(line, sizeof(line), stdin) == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        ProtocolCommand command;
        ProtocolError error;
        if (parse_command_line(line, &command, &error)) {
            g_last_seq = command.seq;
            g_channels.apply_command(command);
            g_fault.note_command(millis_now(), command.ttl_ms);
        } else {
            std::printf("{\"type\":\"protocol_error\",\"error\":\"%s\"}\n", protocol_error_name(error));
            std::fflush(stdout);
        }
    }
}

void control_task(void*) {
    constexpr float dt_s = 0.001f;
    while (true) {
        g_fault.update(millis_now(), estop_ok(), local_fault_pins_ok());
        const bool global_enable = !g_fault.faulted();
        g_channels.update(dt_s, global_enable);
        g_spwm.tick(g_channels, dt_s, global_enable);
        g_adc.sample(g_channels);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void telemetry_task(void*) {
    while (true) {
        g_telemetry.report_status(KUGLASS_CONTROLLER_ID, g_last_seq, g_channels, g_fault);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

void configure_gpio_inputs() {
#ifdef ESP_PLATFORM
    uint64_t mask = 1ULL << KUGLASS_PINMAP.estop_n_gpio;
    for (size_t i = 0; i < KUGLASS_LOCAL_CHANNELS; ++i) {
        mask |= 1ULL << KUGLASS_PINMAP.channel[i].fault_n_gpio;
    }
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pin_bit_mask = mask;
    gpio_config(&cfg);
#endif
}

}  // namespace

extern "C" void app_main(void) {
    configure_gpio_inputs();
    g_channels.begin(static_cast<uint8_t>(KUGLASS_FIRST_CHANNEL));
    g_fault.begin();
    g_adc.begin();
    g_spwm.begin(KUGLASS_PINMAP);

#ifdef ESP_PLATFORM
    ESP_LOGI("kuglass", "controller %s starting, channels %d-%d", KUGLASS_CONTROLLER_ID, KUGLASS_FIRST_CHANNEL, KUGLASS_FIRST_CHANNEL + 3);
    xTaskCreate(serial_rx_task, "serial_rx", 4096, nullptr, 7, nullptr);
    xTaskCreate(control_task, "control", 4096, nullptr, 9, nullptr);
    xTaskCreate(telemetry_task, "telemetry", 4096, nullptr, 5, nullptr);
#endif
}

