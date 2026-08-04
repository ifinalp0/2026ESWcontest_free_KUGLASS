#pragma once

#include "sensor_snapshot.h"

#include <cstdint>

void merge_camera_sample(const SensorSnapshot& sample, SensorSnapshot* destination);
void merge_temperature_sample(float temperature_c,
                              uint32_t timestamp_ms,
                              SensorSnapshot* destination);
void invalidate_camera_sample_state(SensorSnapshot* snapshot);
void invalidate_temperature_sample_state(SensorSnapshot* snapshot);
void invalidate_stale_sensors(uint32_t now_ms,
                              uint32_t camera_max_age_ms,
                              uint32_t temperature_max_age_ms,
                              SensorSnapshot* snapshot);
