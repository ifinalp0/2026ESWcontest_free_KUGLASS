#pragma once

#include "kuglass_config.h"
#include "sdkconfig.h"

#include <cstddef>

namespace camera_pins {

#if !CONFIG_IDF_TARGET_ESP32S3
#error "The ESP32_A camera profile requires an ESP32-S3 target."
#endif

#if !CONFIG_ESPTOOLPY_FLASHSIZE_8MB
#error "The ESP32-S3-DevKitC-1U-N8R8 profile requires 8 MB flash."
#endif

#if !CONFIG_SPIRAM || !CONFIG_SPIRAM_MODE_OCT
#error "The ESP32-S3-DevKitC-1U-N8R8 profile requires Octal PSRAM."
#endif

#if !CONFIG_SPIRAM_SPEED_80M
#error "The validated camera profile requires 80 MHz PSRAM."
#endif

// The supplied 18-pin OV2640 module has its own 12 MHz oscillator and does
// not expose XCLK. DCLK on the module is the camera PCLK output.
static constexpr int kXclk = -1;
static constexpr int kXclkFrequencyHz = 12000000;

static constexpr const char* kProfileName = "ESP32-S3-DevKitC-1U-N8R8";
static constexpr int kSiod = 5;
static constexpr int kSioc = 4;
static constexpr int kVsync = 6;
static constexpr int kHref = 7;
static constexpr int kPclk = 8;
static constexpr int kD0 = 9;
static constexpr int kD1 = 10;
static constexpr int kD2 = 11;
static constexpr int kD3 = 12;
static constexpr int kD4 = 13;
static constexpr int kD5 = 14;
static constexpr int kD6 = 15;
static constexpr int kD7 = 16;
static constexpr int kReset = 17;
static constexpr int kPwdn = 18;

static constexpr int kAssignedPins[] = {
    kSiod, kSioc, kVsync, kHref, kPclk, kD0, kD1, kD2,
    kD3,   kD4,   kD5,    kD6,   kD7,   kReset, kPwdn,
};

constexpr bool is_camera_pin(int pin) {
    for (std::size_t i = 0;
         i < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++i) {
        if (kAssignedPins[i] == pin) return true;
    }
    return false;
}

constexpr bool assigned_pins_are_unique() {
    for (std::size_t i = 0;
         i < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++i) {
        for (std::size_t j = i + 1;
             j < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++j) {
            if (kAssignedPins[i] == kAssignedPins[j]) return false;
        }
    }
    return true;
}

constexpr bool is_devkit_reserved_pin(int pin) {
    // Boot straps: 0, 3, 45, 46; DevKit USB Serial/JTAG: 19, 20; module-internal: 26..34;
    // unavailable with Octal PSRAM: 35..37; RGB LED: 38/48;
    // USB-to-UART bridge: 43, 44.
    return pin == 0 || pin == 3 || pin == 19 || pin == 20 ||
           (pin >= 26 && pin <= 38) || pin == 43 || pin == 44 ||
           pin == 45 || pin == 46 || pin == 48;
}

constexpr bool is_valid_esp32s3_gpio(int pin) {
    return (pin >= 0 && pin <= 21) || (pin >= 26 && pin <= 48);
}

constexpr bool is_available_control_pin(int pin) {
    return is_valid_esp32s3_gpio(pin) && !is_devkit_reserved_pin(pin) &&
           !is_camera_pin(pin);
}

constexpr bool assigned_pins_avoid_devkit_resources() {
    for (std::size_t i = 0;
         i < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++i) {
        if (is_devkit_reserved_pin(kAssignedPins[i])) return false;
    }
    return true;
}

constexpr bool assigned_pins_are_valid() {
    for (std::size_t i = 0;
         i < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++i) {
        if (!is_valid_esp32s3_gpio(kAssignedPins[i])) return false;
    }
    return true;
}

static_assert(assigned_pins_are_unique(), "Camera GPIO assignments must be unique.");
static_assert(assigned_pins_are_valid(), "Camera GPIO must exist on ESP32-S3.");
static_assert(assigned_pins_avoid_devkit_resources(),
              "Camera GPIO conflicts with an ESP32-S3 DevKit resource.");
static_assert(is_available_control_pin(KUGLASS_B_UART_TX_GPIO),
              "ESP32_B UART TX must use an available output-capable DevKit pin.");
static_assert(is_available_control_pin(KUGLASS_B_UART_RX_GPIO),
              "ESP32_B UART RX must use an available DevKit pin.");
static_assert(is_available_control_pin(KUGLASS_DS18B20_GPIO),
              "DS18B20 must use an available bidirectional DevKit pin.");
static_assert(KUGLASS_B_UART_TX_GPIO != KUGLASS_B_UART_RX_GPIO &&
                  KUGLASS_B_UART_TX_GPIO != KUGLASS_DS18B20_GPIO &&
                  KUGLASS_B_UART_RX_GPIO != KUGLASS_DS18B20_GPIO,
              "ESP32_A non-camera GPIO assignments must be unique.");

}  // namespace camera_pins
