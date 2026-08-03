#include "camera_metric_adapter.h"
#include "ds18b20_sensor.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    constexpr uint16_t width = 4;
    constexpr uint16_t height = 2;
    std::vector<uint8_t> frame(static_cast<size_t>(width) * height * 2U);
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 2U;
            const uint16_t pixel = x < width / 2U ? 0xffffU : 0x0000U;
            frame[offset] = static_cast<uint8_t>(pixel >> 8U);
            frame[offset + 1] = static_cast<uint8_t>(pixel & 0xffU);
        }
    }

    CameraRoiMetrics left;
    CameraRoiMetrics right;
    assert(analyze_rgb565_frame(frame.data(), frame.size(), width, height, &left, &right));
    assert(left.mean_brightness > 0.99f);
    assert(left.saturation_ratio > 0.99f);
    assert(right.mean_brightness < 0.01f);
    assert(right.saturation_ratio < 0.01f);
    assert(!analyze_rgb565_frame(frame.data(), frame.size() - 1, width, height, &left, &right));

    const uint8_t scratchpad[9] = {0x50, 0x05, 0x4b, 0x46, 0x7f, 0xff, 0x0c, 0x10, 0x1c};
    assert(ds18b20_crc8(scratchpad, 8) == scratchpad[8]);
    uint8_t corrupt[9];
    for (size_t i = 0; i < 9; ++i) corrupt[i] = scratchpad[i];
    corrupt[2] ^= 0x01U;
    assert(ds18b20_crc8(corrupt, 8) != corrupt[8]);

    std::puts("sensor adapters ok");
    return 0;
}
