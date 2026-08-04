#pragma once

#include "sensor_snapshot.h"

#include <cstddef>
#include <cstdint>

// Converts the ESP32_A-owned RGB565 QVGA frame into the scalar metrics
// consumed by the policy engine. RGB565 bytes are big-endian, matching the
// validated standalone camera reference capture path.
bool analyze_rgb565_frame(const uint8_t* data,
                          size_t data_size,
                          uint16_t width,
                          uint16_t height,
                          CameraRoiMetrics* left,
                          CameraRoiMetrics* right);

class CameraMetricAdapter {
public:
    bool begin();
    bool sample(SensorSnapshot* snapshot);
    void stop();
    bool available() const { return available_; }

private:
    bool available_ = false;
    [[maybe_unused]] uint32_t frame_id_ = 0;
};
