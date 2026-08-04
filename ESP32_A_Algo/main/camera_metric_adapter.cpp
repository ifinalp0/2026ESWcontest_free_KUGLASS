#include "camera_metric_adapter.h"

#include "kuglass_config.h"

#include <cmath>

#if defined(ESP_PLATFORM) && KUGLASS_ONBOARD_CAMERA
#include "camera_service.h"
#include "esp_camera.h"
#include "esp_err.h"
#endif

namespace {

uint8_t rgb565_luma(const uint8_t* pixel) {
    const uint16_t packed =
        static_cast<uint16_t>(static_cast<uint16_t>(pixel[0]) << 8U) |
        static_cast<uint16_t>(pixel[1]);
    const uint8_t red = static_cast<uint8_t>(((packed >> 11U) & 0x1fU) * 255U / 31U);
    const uint8_t green = static_cast<uint8_t>(((packed >> 5U) & 0x3fU) * 255U / 63U);
    const uint8_t blue = static_cast<uint8_t>((packed & 0x1fU) * 255U / 31U);
    return static_cast<uint8_t>((77U * red + 150U * green + 29U * blue) >> 8U);
}
struct RoiAccumulator {
    uint64_t luma_sum = 0;
    uint32_t pixels = 0;
    uint32_t saturated = 0;
    uint32_t highlighted = 0;
    uint32_t edges = 0;
    uint32_t edge_tests = 0;
};

CameraRoiMetrics finish(const RoiAccumulator& accumulator) {
    CameraRoiMetrics result;
    if (accumulator.pixels == 0) {
        return result;
    }
    result.mean_brightness =
        static_cast<float>(accumulator.luma_sum) /
        (255.0f * static_cast<float>(accumulator.pixels));
    result.saturation_ratio =
        static_cast<float>(accumulator.saturated) / static_cast<float>(accumulator.pixels);
    result.highlight_area =
        static_cast<float>(accumulator.highlighted) / static_cast<float>(accumulator.pixels);
    result.edge_density = accumulator.edge_tests == 0
                              ? 0.0f
                              : static_cast<float>(accumulator.edges) /
                                    static_cast<float>(accumulator.edge_tests);
    return result;
}

}  // namespace

bool analyze_rgb565_frame(const uint8_t* data,
                          size_t data_size,
                          uint16_t width,
                          uint16_t height,
                          CameraRoiMetrics* left,
                          CameraRoiMetrics* right) {
    if (data == nullptr || left == nullptr || right == nullptr || width < 2 || height < 2) {
        return false;
    }
    const size_t expected_size = static_cast<size_t>(width) * height * 2U;
    if (data_size != expected_size) {
        return false;
    }

    RoiAccumulator accumulators[2];
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * width + x) * 2U;
            const uint8_t luma = rgb565_luma(data + offset);
            RoiAccumulator& accumulator = accumulators[x < width / 2U ? 0 : 1];
            accumulator.luma_sum += luma;
            ++accumulator.pixels;
            if (luma >= 245U) {
                ++accumulator.saturated;
            }
            if (luma >= 230U) {
                ++accumulator.highlighted;
            }

            if (x > 0 && x != width / 2U) {
                const uint8_t previous = rgb565_luma(data + offset - 2U);
                ++accumulator.edge_tests;
                if (std::abs(static_cast<int>(luma) - static_cast<int>(previous)) >= 22) {
                    ++accumulator.edges;
                }
            }
            if (y > 0) {
                const uint8_t previous =
                    rgb565_luma(data + offset - static_cast<size_t>(width) * 2U);
                ++accumulator.edge_tests;
                if (std::abs(static_cast<int>(luma) - static_cast<int>(previous)) >= 22) {
                    ++accumulator.edges;
                }
            }
        }
    }

    *left = finish(accumulators[0]);
    *right = finish(accumulators[1]);
    return true;
}

bool CameraMetricAdapter::begin() {
#if defined(ESP_PLATFORM) && KUGLASS_ONBOARD_CAMERA
    if (available_) return true;
    available_ = camera_service_start() == ESP_OK;
#else
    available_ = false;
#endif
    return available_;
}

void CameraMetricAdapter::stop() {
#if defined(ESP_PLATFORM) && KUGLASS_ONBOARD_CAMERA
    camera_service_stop();
#endif
    available_ = false;
}

bool CameraMetricAdapter::sample(SensorSnapshot* snapshot) {
    if (!available_ || snapshot == nullptr) {
        return false;
    }
#if defined(ESP_PLATFORM) && KUGLASS_ONBOARD_CAMERA
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
        return false;
    }
    const uint64_t capture_timestamp_ms =
        static_cast<uint64_t>(frame->timestamp.tv_sec) * 1000ULL +
        static_cast<uint64_t>(frame->timestamp.tv_usec) / 1000ULL;
    CameraRoiMetrics left;
    CameraRoiMetrics right;
    const bool valid = frame->format == PIXFORMAT_RGB565 &&
                       frame->width <= UINT16_MAX && frame->height <= UINT16_MAX &&
                       analyze_rgb565_frame(frame->buf,
                                            frame->len,
                                            static_cast<uint16_t>(frame->width),
                                            static_cast<uint16_t>(frame->height),
                                            &left,
                                            &right);
    esp_camera_fb_return(frame);
    if (!valid) {
        return false;
    }
    snapshot->front_left = left;
    snapshot->front_right = right;
    snapshot->camera_valid = true;
    snapshot->camera_frame_id = ++frame_id_;
    snapshot->camera_timestamp_ms = static_cast<uint32_t>(capture_timestamp_ms);
    return true;
#else
    return false;
#endif
}
