#pragma once

#include <cstddef>
#include <cstdint>

uint8_t ds18b20_crc8(const uint8_t* data, size_t size);

class Ds18b20Sensor {
public:
    explicit Ds18b20Sensor(int gpio_num) : gpio_num_(gpio_num) {}

    bool begin();
    // Non-blocking at the 750 ms conversion scale. Returns true only when a
    // newly CRC-validated reading has been written to temperature_c.
    bool poll(uint32_t now_ms, float* temperature_c);
    bool available() const { return available_; }

private:
    bool reset_bus();
    void write_bit(bool value);
    bool read_bit();
    void write_byte(uint8_t value);
    uint8_t read_byte();
    bool start_conversion(uint32_t now_ms);
    bool read_temperature(float* temperature_c);

    [[maybe_unused]] int gpio_num_;
    bool available_ = false;
    bool conversion_pending_ = false;
    uint32_t conversion_started_ms_ = 0;
    [[maybe_unused]] uint32_t retry_after_ms_ = 0;
};
