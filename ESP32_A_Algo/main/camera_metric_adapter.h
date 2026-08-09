#pragma once

#include "sensor_snapshot.h"

#include <cstddef>
#include <cstdint>

// Converts the display-correct ESP32_A RGB565 VGA frame into the scalar
// metrics consumed by the policy engine. The first half of each row is the
// driver's side as shown on the left of TabUI; the second half is the
// passenger's side as shown on the right. RGB565 bytes are big-endian,
// matching the validated standalone camera reference capture path.
bool analyze_rgb565_frame(const uint8_t* data,
                          size_t data_size,
                          uint16_t width,
                          uint16_t height,
                          CameraRoiMetrics* driver_left,
                          CameraRoiMetrics* passenger_right);

struct CameraJpegFrame {
    uint8_t* data = nullptr;
    size_t size = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

void release_camera_jpeg_frame(CameraJpegFrame* frame);

class CameraMetricAdapter {
public:
    bool begin();
    // jpeg_frame is optional. Scalar metrics are still produced if JPEG
    // encoding fails, so video viewing cannot stop the control policy input.
    bool sample(SensorSnapshot* snapshot, CameraJpegFrame* jpeg_frame = nullptr);
    void stop();
    bool available() const { return available_; }

private:
    bool available_ = false;
    [[maybe_unused]] uint32_t frame_id_ = 0;
};
