#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

enum class JsonLineResult : uint8_t {
    NONE = 0,
    COMPLETE,
    OVERSIZE_DROPPED,
    OUTPUT_TOO_SMALL,
};

// Allocation-free JSONL framing shared by the UART receive path and host tests.
// Once a line exceeds the internal capacity, every remaining byte in that line
// is ignored until '\n'.  This prevents the tail of one oversized frame from
// being interpreted as a new command or status frame.
class JsonLineAccumulator {
public:
    static constexpr size_t kCapacity = 1024;

    JsonLineResult feed(uint8_t byte, char* output, size_t output_size) {
        if (discard_until_newline_) {
            if (byte == '\n') {
                discard_until_newline_ = false;
                return JsonLineResult::OVERSIZE_DROPPED;
            }
            return JsonLineResult::NONE;
        }

        if (byte == '\r') return JsonLineResult::NONE;
        if (byte == '\n') {
            if (length_ == 0) return JsonLineResult::NONE;
            if (output == nullptr || output_size <= length_) {
                length_ = 0;
                return JsonLineResult::OUTPUT_TOO_SMALL;
            }
            std::memcpy(output, buffer_, length_);
            output[length_] = '\0';
            length_ = 0;
            return JsonLineResult::COMPLETE;
        }

        if (length_ >= sizeof(buffer_) - 1) {
            length_ = 0;
            discard_until_newline_ = true;
            return JsonLineResult::NONE;
        }
        buffer_[length_++] = static_cast<char>(byte);
        return JsonLineResult::NONE;
    }

    bool discarding() const { return discard_until_newline_; }

private:
    char buffer_[kCapacity] = {};
    size_t length_ = 0;
    bool discard_until_newline_ = false;
};
