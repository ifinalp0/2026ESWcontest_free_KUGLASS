#pragma once

// The OV2640 is mounted opposite to the direction used by the policy and
// TabUI. Apply the 180-degree correction in the sensor before a frame enters
// either consumer. A 180-degree rotation is horizontal mirror + vertical flip.
enum class CameraOrientationResult {
    OK,
    UNSUPPORTED,
    HORIZONTAL_MIRROR_FAILED,
    VERTICAL_FLIP_FAILED,
};

template <typename Sensor>
CameraOrientationResult apply_camera_capture_orientation(Sensor* sensor) {
    if (sensor == nullptr || sensor->set_hmirror == nullptr ||
        sensor->set_vflip == nullptr) {
        return CameraOrientationResult::UNSUPPORTED;
    }
    if (sensor->set_hmirror(sensor, 1) != 0) {
        return CameraOrientationResult::HORIZONTAL_MIRROR_FAILED;
    }
    if (sensor->set_vflip(sensor, 1) != 0) {
        return CameraOrientationResult::VERTICAL_FLIP_FAILED;
    }
    return CameraOrientationResult::OK;
}
