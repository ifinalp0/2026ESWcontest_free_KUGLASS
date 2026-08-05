#include "camera_metric_adapter.h"

#include "kuglass_config.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

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

#if defined(ESP_PLATFORM) && KUGLASS_ONBOARD_CAMERA
struct BoundedJpegBuffer {
    uint8_t* data = nullptr;
    size_t capacity = 0;
    size_t size = 0;
    bool overflow = false;
};

size_t append_jpeg_bytes(void* argument,
                         size_t index,
                         const void* data,
                         size_t size) {
    auto* output = static_cast<BoundedJpegBuffer*>(argument);
    // jpge signals end-of-image with a null, zero-length callback.
    if (output != nullptr && index == output->size && data == nullptr && size == 0U) {
        return 0;
    }
    if (output == nullptr || output->data == nullptr || index != output->size ||
        data == nullptr || output->size > output->capacity ||
        size > output->capacity - output->size) {
        if (output != nullptr) output->overflow = true;
        return 0;
    }
    std::memcpy(output->data + output->size, data, size);
    output->size += size;
    return size;
}

bool encode_camera_jpeg(camera_fb_t* frame, CameraJpegFrame* jpeg_frame) {
    BoundedJpegBuffer output;
    output.capacity = KUGLASS_CAMERA_JPEG_MAX_BYTES;
    output.data = static_cast<uint8_t*>(std::malloc(output.capacity));
    if (output.data == nullptr) return false;

    const bool encoded = frame2jpg_cb(
        frame, KUGLASS_CAMERA_JPEG_QUALITY, append_jpeg_bytes, &output);
    const bool valid = encoded && !output.overflow && output.size >= 4U &&
                       output.data[0] == 0xffU && output.data[1] == 0xd8U &&
                       output.data[output.size - 2U] == 0xffU &&
                       output.data[output.size - 1U] == 0xd9U;
    if (!valid) {
        std::free(output.data);
        return false;
    }

    jpeg_frame->data = output.data;
    jpeg_frame->size = output.size;
    jpeg_frame->width = static_cast<uint16_t>(frame->width);
    jpeg_frame->height = static_cast<uint16_t>(frame->height);
    return true;
}
#endif

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

    // VGA has four times as many pixels as the former QVGA path. Cache one
    // luma row so each pixel is converted only once while preserving the exact
    // horizontal and vertical edge tests used by the policy metrics.
    auto* previous_row = static_cast<uint8_t*>(std::malloc(width));
    if (previous_row == nullptr) return false;

    RoiAccumulator accumulators[2];
    for (uint16_t y = 0; y < height; ++y) {
        uint8_t previous_horizontal = 0;
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
                ++accumulator.edge_tests;
                if (std::abs(static_cast<int>(luma) -
                             static_cast<int>(previous_horizontal)) >= 22) {
                    ++accumulator.edges;
                }
            }
            if (y > 0) {
                ++accumulator.edge_tests;
                if (std::abs(static_cast<int>(luma) -
                             static_cast<int>(previous_row[x])) >= 22) {
                    ++accumulator.edges;
                }
            }
            previous_row[x] = luma;
            previous_horizontal = luma;
        }
    }

    *left = finish(accumulators[0]);
    *right = finish(accumulators[1]);
    std::free(previous_row);
    return true;
}

bool CameraMetricAdapter::begin() {
#if defined(ESP_PLATFORM) && KUGLASS_ONBOARD_CAMERA
    if (available_) return true;
    available_ = camera_service_start() == ESP_OK;
    if (available_) {
        // Match the validated ESP_Camera RGB565 -> JPEG conversion contract.
        jpgSetChroma(CHROMA_420);
        jpgSetRgb565BE(true);
    }
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

void release_camera_jpeg_frame(CameraJpegFrame* frame) {
    if (frame == nullptr) return;
    std::free(frame->data);
    *frame = CameraJpegFrame{};
}

bool CameraMetricAdapter::sample(SensorSnapshot* snapshot, CameraJpegFrame* jpeg_frame) {
    if (jpeg_frame != nullptr) {
        release_camera_jpeg_frame(jpeg_frame);
    }
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
    if (valid && jpeg_frame != nullptr) {
        (void)encode_camera_jpeg(frame, jpeg_frame);
    }
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
