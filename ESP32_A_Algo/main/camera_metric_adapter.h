#pragma once

#include "sensor_snapshot.h"

#include <cstddef>
#include <cstdint>

// Converts the ESP_Camera RGB565 QVGA frame into the scalar metrics
// consumed by the policy engine. RGB565 bytes are big-endian, matching the
// validated ESP_Camera capture/encoder path.
bool analyze_rgb565_frame(const uint8_t* data,
                          size_t data_size,
                          uint16_t width,
                          uint16_t height,
                          CameraRoiMetrics* left,
                          CameraRoiMetrics* right);

class CameraMetricAdapter {
public:
    bool begin();
    bool sample(uint32_t now_ms, SensorSnapshot* snapshot);
    bool available() const { return available_; }

private:
    bool available_ = false;
    [[maybe_unused]] uint32_t frame_id_ = 0;
};
