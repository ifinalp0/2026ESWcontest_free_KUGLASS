/*
 * ESP32_B Logic Carrier GPIO connection test
 * Target: ESP32-S3-DevKitC-1-N8R8 / ESP-IDF 5.x
 *
 * This firmware deliberately does not create PWM or SPWM. It drives only one
 * of the twelve MCU output signals HIGH at a time so it can be checked with a
 * digital multimeter. EN_GLOBAL, FAULT_N and ADC pins always remain inputs.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHANNEL_COUNT          4U
#define STARTUP_LOW_MS         5000U
#define PREPARE_LOW_MS         3000U
#define TEST_HIGH_MS           10000U

#define GPIO_BIT(number)       (1ULL << (number))

#define OUTPUT_GPIO_MASK                                                   \
    (GPIO_BIT(10) | GPIO_BIT(11) | GPIO_BIT(12) |                         \
     GPIO_BIT(14) | GPIO_BIT(15) | GPIO_BIT(16) |                         \
     GPIO_BIT(18) | GPIO_BIT(21) | GPIO_BIT(38) |                         \
     GPIO_BIT(40) | GPIO_BIT(41) | GPIO_BIT(42))

#define SAFETY_INPUT_GPIO_MASK                                             \
    (GPIO_BIT(13) | GPIO_BIT(17) | GPIO_BIT(19) |                         \
     GPIO_BIT(39) | GPIO_BIT(47))

#define ADC_INPUT_GPIO_MASK                                                \
    (GPIO_BIT(1) | GPIO_BIT(2) | GPIO_BIT(3) | GPIO_BIT(4) |              \
     GPIO_BIT(5) | GPIO_BIT(6) | GPIO_BIT(7) | GPIO_BIT(8))

_Static_assert(__builtin_popcountll(OUTPUT_GPIO_MASK) == 12,
               "The twelve output GPIOs must be unique");
_Static_assert((OUTPUT_GPIO_MASK & SAFETY_INPUT_GPIO_MASK) == 0,
               "An output GPIO conflicts with a safety input");
_Static_assert((OUTPUT_GPIO_MASK & ADC_INPUT_GPIO_MASK) == 0,
               "An output GPIO conflicts with an ADC input");

typedef enum {
    SIGNAL_PWM_MAG,
    SIGNAL_DIRECTION,
    SIGNAL_ENABLE,
} signal_kind_t;

typedef struct {
    gpio_num_t gpio;
    unsigned channel;
    signal_kind_t kind;
    const char *name;
    const char *u3_header_pin;
    unsigned j7_pin;
} test_pin_t;

/*
 * Keep this table in channel/signal order. J7 ENABLE is the output of the
 * 74HC08 gate, so it is HIGH only while EN_GLOBAL is also HIGH.
 */
static const test_pin_t TEST_PINS[] = {
    {GPIO_NUM_10, 0, SIGNAL_PWM_MAG,   "PWM_MAG_CH0", "J1-16",  1},
    {GPIO_NUM_11, 0, SIGNAL_DIRECTION, "DIR_CH0",     "J1-17",  3},
    {GPIO_NUM_12, 0, SIGNAL_ENABLE,    "ENABLE_CH0",  "J1-18",  5},
    {GPIO_NUM_14, 1, SIGNAL_PWM_MAG,   "PWM_MAG_CH1", "J1-20", 17},
    {GPIO_NUM_15, 1, SIGNAL_DIRECTION, "DIR_CH1",     "J1-8",  19},
    {GPIO_NUM_16, 1, SIGNAL_ENABLE,    "ENABLE_CH1",  "J1-9",  21},
    {GPIO_NUM_18, 2, SIGNAL_PWM_MAG,   "PWM_MAG_CH2", "J1-11", 33},
    {GPIO_NUM_21, 2, SIGNAL_DIRECTION, "DIR_CH2",     "J3-18", 35},
    {GPIO_NUM_38, 2, SIGNAL_ENABLE,    "ENABLE_CH2",  "J3-10", 37},
    {GPIO_NUM_40, 3, SIGNAL_PWM_MAG,   "PWM_MAG_CH3", "J3-8",  49},
    {GPIO_NUM_41, 3, SIGNAL_DIRECTION, "DIR_CH3",     "J3-7",  51},
    {GPIO_NUM_42, 3, SIGNAL_ENABLE,    "ENABLE_CH3",  "J3-6",  53},
};

static const gpio_num_t PWM_MAG_GPIO[CHANNEL_COUNT] = {
    GPIO_NUM_10, GPIO_NUM_14, GPIO_NUM_18, GPIO_NUM_40,
};

static const gpio_num_t DIRECTION_GPIO[CHANNEL_COUNT] = {
    GPIO_NUM_11, GPIO_NUM_15, GPIO_NUM_21, GPIO_NUM_41,
};

static const gpio_num_t ENABLE_GPIO[CHANNEL_COUNT] = {
    GPIO_NUM_12, GPIO_NUM_16, GPIO_NUM_38, GPIO_NUM_42,
};

static const gpio_num_t FAULT_N_GPIO[CHANNEL_COUNT] = {
    GPIO_NUM_13, GPIO_NUM_17, GPIO_NUM_39, GPIO_NUM_47,
};

static const gpio_num_t EN_GLOBAL_GPIO = GPIO_NUM_19;
static const char *TAG = "ESP32_PIN_TEST";

static void force_all_outputs_low(void)
{
    /* Disable first, then clear PWM magnitude and direction. */
    for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        ESP_ERROR_CHECK(gpio_set_level(ENABLE_GPIO[channel], 0));
    }
    for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        ESP_ERROR_CHECK(gpio_set_level(PWM_MAG_GPIO[channel], 0));
        ESP_ERROR_CHECK(gpio_set_level(DIRECTION_GPIO[channel], 0));
    }
}

static void configure_outputs(void)
{
    /* Preload LOW before changing the pads to output mode. */
    force_all_outputs_low();

    gpio_config_t config = {0};
    config.pin_bit_mask = OUTPUT_GPIO_MASK;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));

    force_all_outputs_low();
}

static void configure_inputs(void)
{
    /* EN_GLOBAL uses the external E-Stop pull-down. */
    gpio_config_t en_global = {0};
    en_global.pin_bit_mask = GPIO_BIT(19);
    en_global.mode = GPIO_MODE_INPUT;
    en_global.pull_up_en = GPIO_PULLUP_DISABLE;
    en_global.pull_down_en = GPIO_PULLDOWN_DISABLE;
    en_global.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&en_global));

    /* FAULT_N signals already have external 10 kohm pull-ups. */
    gpio_config_t fault_inputs = {0};
    fault_inputs.pin_bit_mask = GPIO_BIT(13) | GPIO_BIT(17) |
                                GPIO_BIT(39) | GPIO_BIT(47);
    fault_inputs.mode = GPIO_MODE_INPUT;
    fault_inputs.pull_up_en = GPIO_PULLUP_DISABLE;
    fault_inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    fault_inputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&fault_inputs));

    /* Reserve all eight ADC connections as input-only in this test. */
    gpio_config_t adc_inputs = {0};
    adc_inputs.pin_bit_mask = ADC_INPUT_GPIO_MASK;
    adc_inputs.mode = GPIO_MODE_INPUT;
    adc_inputs.pull_up_en = GPIO_PULLUP_DISABLE;
    adc_inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    adc_inputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&adc_inputs));
}

static void log_safety_inputs(void)
{
    ESP_LOGI(TAG,
             "inputs: EN_GLOBAL(GPIO19)=%d, "
             "FAULT_N[CH0..CH3]=%d,%d,%d,%d (LOW means fault)",
             gpio_get_level(EN_GLOBAL_GPIO),
             gpio_get_level(FAULT_N_GPIO[0]),
             gpio_get_level(FAULT_N_GPIO[1]),
             gpio_get_level(FAULT_N_GPIO[2]),
             gpio_get_level(FAULT_N_GPIO[3]));
}

static void run_one_pin_test(size_t index)
{
    const test_pin_t *pin = &TEST_PINS[index];
    const bool gated_enable = pin->kind == SIGNAL_ENABLE;

    force_all_outputs_low();
    ESP_LOGI(TAG,
             "[%u/%u] LOW prepare: CH%u %s, GPIO%d, U3-%s, J7-%u (%u ms)",
             (unsigned)(index + 1), (unsigned)(sizeof(TEST_PINS) / sizeof(TEST_PINS[0])),
             pin->channel, pin->name, (int)pin->gpio, pin->u3_header_pin, pin->j7_pin,
             (unsigned)PREPARE_LOW_MS);
    vTaskDelay(pdMS_TO_TICKS(PREPARE_LOW_MS));

    ESP_ERROR_CHECK(gpio_set_level(pin->gpio, 1));
    ESP_LOGI(TAG,
             "[%u/%u] HIGH now: CH%u %s, GPIO%d, U3-%s, J7-%u (%u ms)%s",
             (unsigned)(index + 1), (unsigned)(sizeof(TEST_PINS) / sizeof(TEST_PINS[0])),
             pin->channel, pin->name, (int)pin->gpio, pin->u3_header_pin, pin->j7_pin,
             (unsigned)TEST_HIGH_MS,
             gated_enable ? " [J7 ENABLE also requires EN_GLOBAL=HIGH]" : "");
    log_safety_inputs();
    vTaskDelay(pdMS_TO_TICKS(TEST_HIGH_MS));

    force_all_outputs_low();
}

void app_main(void)
{
    /* Output latches and modes are established before logging or other work. */
    configure_outputs();
    configure_inputs();

    ESP_LOGI(TAG, "ESP32_B static GPIO test started; PWM/SPWM is disabled");
    ESP_LOGI(TAG, "all 12 outputs stay LOW for %u ms before the first test",
             (unsigned)STARTUP_LOW_MS);
    ESP_LOGW(TAG, "disconnect Power Stage, PDLC and 12/24 V or HV before testing");
    log_safety_inputs();
    vTaskDelay(pdMS_TO_TICKS(STARTUP_LOW_MS));

    uint32_t cycle = 1;
    while (true) {
        ESP_LOGI(TAG, "========== test cycle %lu ==========",
                 (unsigned long)cycle);
        for (size_t index = 0; index < sizeof(TEST_PINS) / sizeof(TEST_PINS[0]); ++index) {
            run_one_pin_test(index);
        }
        force_all_outputs_low();
        ESP_LOGI(TAG, "cycle %lu complete; restarting after %u ms LOW",
                 (unsigned long)cycle, (unsigned)STARTUP_LOW_MS);
        vTaskDelay(pdMS_TO_TICKS(STARTUP_LOW_MS));
        ++cycle;
    }
}
