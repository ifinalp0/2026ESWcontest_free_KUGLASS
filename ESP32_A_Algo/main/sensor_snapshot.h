#pragma once

#include <cstdint>

struct CameraRoiMetrics {
    float mean_brightness = 0.0f;
    float saturation_ratio = 0.0f;
    float highlight_area = 0.0f;
    float edge_density = 0.0f;
};

struct SensorSnapshot {
    CameraRoiMetrics front_left;
    CameraRoiMetrics front_right;
    bool camera_valid = false;
    uint32_t camera_frame_id = 0;
    uint32_t camera_timestamp_ms = 0;
    bool ae_metadata_valid = false;
    float exposure_us = 0.0f;
    float analog_gain = 0.0f;
    float digital_gain = 0.0f;

    float internal_temp_c = 25.0f;
    bool internal_temp_valid = false;
    uint32_t internal_temp_timestamp_ms = 0;

};

inline bool timestamp_fresh(uint32_t now_ms, uint32_t timestamp_ms, uint32_t max_age_ms) {
    return timestamp_ms != 0 && static_cast<uint32_t>(now_ms - timestamp_ms) <= max_age_ms;
}
