#include "camera_metric_adapter.h"
#include "ds18b20_sensor.h"
#include "sensor_state.h"

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
    assert(left.highlight_area > 0.99f);
    assert(left.edge_density < 0.01f);
    assert(right.mean_brightness < 0.01f);
    assert(right.saturation_ratio < 0.01f);
    assert(right.highlight_area < 0.01f);
    assert(right.edge_density < 0.01f);
    assert(!analyze_rgb565_frame(frame.data(), frame.size() - 1, width, height, &left, &right));

    // Verify byte order with colors that are not invariant under byte swap.
    std::vector<uint8_t> endian_frame = {
        0xf8, 0x00, 0xf8, 0x00, 0x00, 0xf8, 0x00, 0xf8,
        0xf8, 0x00, 0xf8, 0x00, 0x00, 0xf8, 0x00, 0xf8,
    };
    assert(analyze_rgb565_frame(endian_frame.data(), endian_frame.size(),
                                width, height, &left, &right));
    assert(left.mean_brightness > right.mean_brightness);

    std::vector<uint8_t> checkerboard(frame.size());
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const uint16_t pixel = ((x + y) & 1U) == 0U ? 0xffffU : 0x0000U;
            const size_t offset = (static_cast<size_t>(y) * width + x) * 2U;
            checkerboard[offset] = static_cast<uint8_t>(pixel >> 8U);
            checkerboard[offset + 1] = static_cast<uint8_t>(pixel & 0xffU);
        }
    }
    assert(analyze_rgb565_frame(checkerboard.data(), checkerboard.size(),
                                width, height, &left, &right));
    assert(left.edge_density > 0.99f);
    assert(right.edge_density > 0.99f);

    assert(timestamp_fresh(1000U, 0U, 1000U));
    assert(!timestamp_fresh(1001U, 0U, 1000U));
    assert(timestamp_fresh(5U, UINT32_MAX - 4U, 10U));
    assert(!timestamp_fresh(6U, UINT32_MAX - 4U, 10U));

    SensorSnapshot merged;
    merge_temperature_sample(31.5f, 500U, &merged);
    SensorSnapshot camera_sample;
    camera_sample.front_left.saturation_ratio = 0.75f;
    camera_sample.camera_valid = true;
    camera_sample.camera_frame_id = 42U;
    camera_sample.camera_timestamp_ms = 900U;
    merge_camera_sample(camera_sample, &merged);
    assert(merged.internal_temp_valid);
    assert(std::fabs(merged.internal_temp_c - 31.5f) < 0.001f);
    assert(merged.internal_temp_timestamp_ms == 500U);
    assert(merged.camera_valid);
    assert(merged.camera_frame_id == 42U);

    invalidate_stale_sensors(1900U, 1000U, 5000U, &merged);
    assert(merged.camera_valid);
    assert(merged.internal_temp_valid);
    invalidate_stale_sensors(1901U, 1000U, 5000U, &merged);
    assert(!merged.camera_valid);
    assert(merged.internal_temp_valid);

    const uint8_t scratchpad[9] = {0x50, 0x05, 0x4b, 0x46, 0x7f, 0xff, 0x0c, 0x10, 0x1c};
    assert(ds18b20_crc8(scratchpad, 8) == scratchpad[8]);
    uint8_t corrupt[9];
    for (size_t i = 0; i < 9; ++i) corrupt[i] = scratchpad[i];
    corrupt[2] ^= 0x01U;
    assert(ds18b20_crc8(corrupt, 8) != corrupt[8]);

    std::puts("sensor adapters ok");
    return 0;
}
