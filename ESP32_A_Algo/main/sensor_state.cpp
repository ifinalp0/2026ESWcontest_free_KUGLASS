#include "sensor_state.h"

void merge_camera_sample(const SensorSnapshot& sample, SensorSnapshot* destination) {
    if (destination == nullptr) return;
    destination->front_left = sample.front_left;
    destination->front_right = sample.front_right;
    destination->camera_valid = sample.camera_valid;
    destination->camera_frame_id = sample.camera_frame_id;
    destination->camera_timestamp_ms = sample.camera_timestamp_ms;
    destination->ae_metadata_valid = sample.ae_metadata_valid;
    destination->exposure_us = sample.exposure_us;
    destination->analog_gain = sample.analog_gain;
    destination->digital_gain = sample.digital_gain;
}

void merge_temperature_sample(float temperature_c,
                              uint32_t timestamp_ms,
                              SensorSnapshot* destination) {
    if (destination == nullptr) return;
    destination->internal_temp_c = temperature_c;
    destination->internal_temp_valid = true;
    destination->internal_temp_timestamp_ms = timestamp_ms;
}

void invalidate_camera_sample_state(SensorSnapshot* snapshot) {
    if (snapshot == nullptr) return;
    snapshot->camera_valid = false;
    snapshot->ae_metadata_valid = false;
}

void invalidate_temperature_sample_state(SensorSnapshot* snapshot) {
    if (snapshot == nullptr) return;
    snapshot->internal_temp_valid = false;
}

void invalidate_stale_sensors(uint32_t now_ms,
                              uint32_t camera_max_age_ms,
                              uint32_t temperature_max_age_ms,
                              SensorSnapshot* snapshot) {
    if (snapshot == nullptr) return;
    if (snapshot->camera_valid &&
        !timestamp_fresh(now_ms, snapshot->camera_timestamp_ms, camera_max_age_ms)) {
        invalidate_camera_sample_state(snapshot);
    }
    if (snapshot->internal_temp_valid &&
        !timestamp_fresh(now_ms, snapshot->internal_temp_timestamp_ms,
                         temperature_max_age_ms)) {
        invalidate_temperature_sample_state(snapshot);
    }
}
