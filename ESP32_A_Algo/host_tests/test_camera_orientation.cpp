#include "camera_orientation.h"

#include <cassert>
#include <cstdio>

namespace {

struct FakeSensor {
    int (*set_hmirror)(FakeSensor*, int) = nullptr;
    int (*set_vflip)(FakeSensor*, int) = nullptr;
    int horizontal_mirror = 0;
    int vertical_flip = 0;
    bool fail_horizontal_mirror = false;
    bool fail_vertical_flip = false;
};

int set_hmirror(FakeSensor* sensor, int enabled) {
    sensor->horizontal_mirror = enabled;
    return sensor->fail_horizontal_mirror ? -1 : 0;
}

int set_vflip(FakeSensor* sensor, int enabled) {
    sensor->vertical_flip = enabled;
    return sensor->fail_vertical_flip ? -1 : 0;
}

FakeSensor make_sensor() {
    FakeSensor sensor;
    sensor.set_hmirror = set_hmirror;
    sensor.set_vflip = set_vflip;
    return sensor;
}

}  // namespace

int main() {
    FakeSensor sensor = make_sensor();
    assert(apply_camera_capture_orientation(&sensor) ==
           CameraOrientationResult::OK);
    assert(sensor.horizontal_mirror == 1);
    assert(sensor.vertical_flip == 1);

    assert(apply_camera_capture_orientation<FakeSensor>(nullptr) ==
           CameraOrientationResult::UNSUPPORTED);

    FakeSensor unsupported;
    assert(apply_camera_capture_orientation(&unsupported) ==
           CameraOrientationResult::UNSUPPORTED);

    FakeSensor horizontal_failure = make_sensor();
    horizontal_failure.fail_horizontal_mirror = true;
    assert(apply_camera_capture_orientation(&horizontal_failure) ==
           CameraOrientationResult::HORIZONTAL_MIRROR_FAILED);
    assert(horizontal_failure.vertical_flip == 0);

    FakeSensor vertical_failure = make_sensor();
    vertical_failure.fail_vertical_flip = true;
    assert(apply_camera_capture_orientation(&vertical_failure) ==
           CameraOrientationResult::VERTICAL_FLIP_FAILED);
    assert(vertical_failure.horizontal_mirror == 1);
    assert(vertical_failure.vertical_flip == 1);

    std::puts("camera orientation ok");
    return 0;
}
