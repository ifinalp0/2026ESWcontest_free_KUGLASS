#include "camera_pins.h"

int main() {
    static_assert(camera_pins::kXclk == -1);
    static_assert(camera_pins::kXclkFrequencyHz == 12000000);
    static_assert(camera_pins::is_devkit_reserved_pin(3));
    static_assert(camera_pins::is_devkit_reserved_pin(19));
    static_assert(camera_pins::is_devkit_reserved_pin(35));
    static_assert(!camera_pins::is_devkit_reserved_pin(47));
    static_assert(!camera_pins::is_devkit_reserved_pin(4));
    static_assert(!camera_pins::is_devkit_reserved_pin(18));
    static_assert(camera_pins::kSioc == 4);
    static_assert(camera_pins::kSiod == 5);
    static_assert(camera_pins::kVsync == 6);
    static_assert(camera_pins::kHref == 7);
    static_assert(camera_pins::kPclk == 8);
    static_assert(camera_pins::kD0 == 9);
    static_assert(camera_pins::kD1 == 10);
    static_assert(camera_pins::kD2 == 11);
    static_assert(camera_pins::kD3 == 12);
    static_assert(camera_pins::kD4 == 13);
    static_assert(camera_pins::kD5 == 14);
    static_assert(camera_pins::kD6 == 15);
    static_assert(camera_pins::kD7 == 16);
    static_assert(camera_pins::kReset == 17);
    static_assert(camera_pins::kPwdn == 18);

    return 0;
}
