/*
 * ESP32_A -> kuglass_esp32_b continuous PWM command test.
 *
 * ESP32_A does not generate the power-stage PWM itself. It continuously sends
 * a fresh JSON Line command to ESP32_B, which generates four-channel SPWM.
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LINK_UART             UART_NUM_1
#define LINK_TX_GPIO          39
#define LINK_RX_GPIO          40
#define LINK_BAUD_RATE        115200
#define LINK_RX_BUFFER_SIZE   1024

#define COMMAND_PERIOD_MS     50U
#define COMMAND_TTL_MS        300U

static const float TEST_MI[4] = {0.80f, 0.65f, 0.40f, 0.55f};
static const char *TAG = "ESP32_A_PWM_TEST";

static void init_link_uart(void)
{
    const uart_config_t config = {
        .baud_rate = LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(
        LINK_UART, LINK_RX_BUFFER_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(
        LINK_UART, LINK_TX_GPIO, LINK_RX_GPIO,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static size_t format_command(char *buffer, size_t buffer_size, uint32_t sequence)
{
    const int length = snprintf(
        buffer, buffer_size,
        "{\"v\":1,\"type\":\"set\",\"seq\":%" PRIu32
        ",\"ttl_ms\":%u,\"enable\":true,"
        "\"mi\":[%.2f,%.2f,%.2f,%.2f]}\n",
        sequence, (unsigned)COMMAND_TTL_MS,
        (double)TEST_MI[0], (double)TEST_MI[1],
        (double)TEST_MI[2], (double)TEST_MI[3]);

    if (length <= 0 || (size_t)length >= buffer_size) {
        return 0;
    }
    return (size_t)length;
}

void app_main(void)
{
    init_link_uart();

    ESP_LOGW(TAG, "CONTINUOUS OUTPUT TEST FIRMWARE");
    ESP_LOGI(TAG, "UART1 TX=GPIO%d RX=GPIO%d, %d baud, 8-N-1",
             LINK_TX_GPIO, LINK_RX_GPIO, LINK_BAUD_RATE);
    ESP_LOGI(TAG, "Sending every %u ms with ttl_ms=%u",
             (unsigned)COMMAND_PERIOD_MS, (unsigned)COMMAND_TTL_MS);
    ESP_LOGI(TAG, "MI=[%.2f, %.2f, %.2f, %.2f]",
             (double)TEST_MI[0], (double)TEST_MI[1],
             (double)TEST_MI[2], (double)TEST_MI[3]);

    uint32_t sequence = 1;
    uint32_t sent_count = 0;
    TickType_t next_wake = xTaskGetTickCount();
    char line[160];

    while (true) {
        const size_t length = format_command(line, sizeof(line), sequence);
        if (length == 0) {
            ESP_LOGE(TAG, "JSON command buffer is too small");
        } else {
            const int written = uart_write_bytes(LINK_UART, line, length);
            if (written != (int)length) {
                ESP_LOGE(TAG, "UART write failed: %d/%u bytes",
                         written, (unsigned)length);
            }
        }

        ++sent_count;
        if ((sent_count % 20U) == 0U) {
            ESP_LOGI(TAG, "command active; last seq=%" PRIu32, sequence);
        }

        ++sequence;
        if (sequence == 0U) {
            /* The current B parser reserves zero as its initial sequence. */
            sequence = 1U;
        }
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(COMMAND_PERIOD_MS));
    }
}
