#include "ds18b20_sensor.h"

#include <cmath>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

namespace {
portMUX_TYPE g_one_wire_mux = portMUX_INITIALIZER_UNLOCKED;
}
#endif

uint8_t ds18b20_crc8(const uint8_t* data, size_t size) {
    if (data == nullptr) {
        return 0;
    }
    uint8_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t value = data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint8_t mix = static_cast<uint8_t>((crc ^ value) & 0x01U);
            crc >>= 1U;
            if (mix != 0U) {
                crc ^= 0x8cU;
            }
            value >>= 1U;
        }
    }
    return crc;
}
bool Ds18b20Sensor::begin() {
#ifdef ESP_PLATFORM
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num_,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&config) != ESP_OK) {
        return false;
    }
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 1);
    available_ = reset_bus();
    return available_;
#else
    return false;
#endif
}

bool Ds18b20Sensor::poll(uint32_t now_ms, float* temperature_c) {
#ifdef ESP_PLATFORM
    if (temperature_c == nullptr || static_cast<int32_t>(now_ms - retry_after_ms_) < 0) {
        return false;
    }
    if (!conversion_pending_) {
        if (!start_conversion(now_ms)) {
            available_ = false;
            retry_after_ms_ = now_ms + 2000U;
        }
        return false;
    }
    if (static_cast<uint32_t>(now_ms - conversion_started_ms_) < 750U) {
        return false;
    }
    conversion_pending_ = false;
    const bool valid = read_temperature(temperature_c);
    available_ = valid;
    retry_after_ms_ = now_ms + (valid ? 250U : 2000U);
    return valid;
#else
    (void)now_ms;
    (void)temperature_c;
    return false;
#endif
}

bool Ds18b20Sensor::reset_bus() {
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&g_one_wire_mux);
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 0);
    esp_rom_delay_us(480);
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 1);
    esp_rom_delay_us(70);
    const bool present = gpio_get_level(static_cast<gpio_num_t>(gpio_num_)) == 0;
    esp_rom_delay_us(410);
    portEXIT_CRITICAL(&g_one_wire_mux);
    return present;
#else
    return false;
#endif
}

void Ds18b20Sensor::write_bit(bool value) {
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&g_one_wire_mux);
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 0);
    esp_rom_delay_us(value ? 6 : 60);
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 1);
    esp_rom_delay_us(value ? 64 : 10);
    portEXIT_CRITICAL(&g_one_wire_mux);
#else
    (void)value;
#endif
}

bool Ds18b20Sensor::read_bit() {
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&g_one_wire_mux);
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 0);
    esp_rom_delay_us(6);
    gpio_set_level(static_cast<gpio_num_t>(gpio_num_), 1);
    esp_rom_delay_us(9);
    const bool value = gpio_get_level(static_cast<gpio_num_t>(gpio_num_)) != 0;
    esp_rom_delay_us(55);
    portEXIT_CRITICAL(&g_one_wire_mux);
    return value;
#else
    return false;
#endif
}

void Ds18b20Sensor::write_byte(uint8_t value) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
        write_bit((value & 0x01U) != 0U);
        value >>= 1U;
    }
}

uint8_t Ds18b20Sensor::read_byte() {
    uint8_t value = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
        if (read_bit()) {
            value |= static_cast<uint8_t>(1U << bit);
        }
    }
    return value;
}

bool Ds18b20Sensor::start_conversion(uint32_t now_ms) {
    if (!reset_bus()) {
        return false;
    }
    write_byte(0xccU);  // Skip ROM; exactly one DS18B20 is supported.
    write_byte(0x44U);  // Convert T. Use an externally powered sensor.
    conversion_pending_ = true;
    conversion_started_ms_ = now_ms;
    available_ = true;
    return true;
}

bool Ds18b20Sensor::read_temperature(float* temperature_c) {
    if (!reset_bus()) {
        return false;
    }
    write_byte(0xccU);
    write_byte(0xbeU);
    uint8_t scratchpad[9] = {};
    for (uint8_t& value : scratchpad) {
        value = read_byte();
    }
    if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) {
        return false;
    }
    const int16_t raw = static_cast<int16_t>(
        static_cast<uint16_t>(scratchpad[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(scratchpad[1]) << 8U));
    const float value = static_cast<float>(raw) / 16.0f;
    if (!std::isfinite(value) || value < -55.0f || value > 125.0f) {
        return false;
    }
    *temperature_c = value;
    return true;
}
