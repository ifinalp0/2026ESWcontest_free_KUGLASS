#pragma once

#include "sdkconfig.h"

#include <cstddef>

namespace camera_pins {

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This project is dedicated to ESP32-S3-DevKitC-1U-N8R8."
#endif

#if !CONFIG_ESPTOOLPY_FLASHSIZE_8MB
#error "ESP32-S3-DevKitC-1U-N8R8 requires an 8 MB flash configuration."
#endif

#if !CONFIG_SPIRAM || !CONFIG_SPIRAM_MODE_OCT
#error "ESP32-S3-DevKitC-1U-N8R8 requires Octal PSRAM support."
#endif

#if !CONFIG_SPIRAM_SPEED_80M
#error "This N8R8 camera profile requires the validated 80 MHz PSRAM setting."
#endif

// The supplied 18-pin module has its own 12 MHz oscillator. It does not expose
// XCLK. DCLK on the module is the camera's PCLK output.
static constexpr int kXclk = -1;
static constexpr int kXclkFrequencyHz = 12000000;

// ESP32-S3-DevKitC-1 header mapping for an ESP32-S3-WROOM-1U module. All
// camera signals are available on header J1. GPIO19/20 remain available for
// native USB, and pins that can be occupied by Octal PSRAM are avoided.
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

constexpr bool assigned_pins_are_unique() {
    for (std::size_t i = 0;
         i < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++i) {
        if (kAssignedPins[i] < 0) {
            continue;
        }
        for (std::size_t j = i + 1;
             j < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++j) {
            if (kAssignedPins[i] == kAssignedPins[j]) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool is_devkit_reserved_pin(int pin) {
    // Boot straps: 0, 3, 45, 46
    // Native USB: 19, 20
    // WROOM module-internal/not broken out: 26..34
    // Unavailable with Octal PSRAM: 35..37
    // On-board RGB LED: 38 on v1.1, 48 on v1.0
    // USB-to-UART bridge: 43, 44
    return pin == 0 || pin == 3 || pin == 19 || pin == 20 ||
           (pin >= 26 && pin <= 38) || pin == 43 || pin == 44 ||
           pin == 45 || pin == 46 || pin == 47 || pin == 48;
}

constexpr bool assigned_pins_avoid_devkit_resources() {
    for (std::size_t i = 0;
         i < sizeof(kAssignedPins) / sizeof(kAssignedPins[0]); ++i) {
        if (is_devkit_reserved_pin(kAssignedPins[i])) {
            return false;
        }
    }
    return true;
}

static_assert(assigned_pins_are_unique(), "Camera GPIO assignments must be unique.");
static_assert(assigned_pins_avoid_devkit_resources(),
              "Camera GPIO conflicts with an ESP32-S3-WROOM-1U DevKit resource.");

}  // namespace camera_pins
