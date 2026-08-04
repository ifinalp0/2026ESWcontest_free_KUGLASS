#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>

// Small, allocation-free cursor for the JSONL control contracts.  It validates
// complete JSON string escapes and the JSON number grammar; callers retain
// schema ownership and decide which fields are required.
class StrictJsonCursor {
public:
    explicit StrictJsonCursor(const char* input) : p_(input) {}

    void skip_ws() {
        while (p_ != nullptr && is_json_ws(*p_)) ++p_;
    }

    bool consume(char expected) {
        skip_ws();
        if (p_ == nullptr || *p_ != expected) return false;
        ++p_;
        return true;
    }

    bool peek(char expected) {
        skip_ws();
        return p_ != nullptr && *p_ == expected;
    }

    bool at_end() {
        skip_ws();
        return p_ != nullptr && *p_ == '\0';
    }

    bool parse_string(char* output, size_t output_size, bool* lossy = nullptr) {
        skip_ws();
        if (p_ == nullptr || *p_ != '"') return false;
        ++p_;
        size_t stored = 0;
        bool value_lossy = false;
        if (output != nullptr && output_size > 0) output[0] = '\0';

        while (*p_ != '\0' && *p_ != '"') {
            const unsigned char raw = static_cast<unsigned char>(*p_++);
            if (raw < 0x20U) return false;
            if (raw != '\\') {
                append_byte(raw, output, output_size, &stored, &value_lossy);
                continue;
            }

            const char escaped = *p_++;
            if (escaped == '\0') return false;
            switch (escaped) {
                case '"': append_byte('"', output, output_size, &stored, &value_lossy); break;
                case '\\': append_byte('\\', output, output_size, &stored, &value_lossy); break;
                case '/': append_byte('/', output, output_size, &stored, &value_lossy); break;
                case 'b': append_byte('\b', output, output_size, &stored, &value_lossy); break;
                case 'f': append_byte('\f', output, output_size, &stored, &value_lossy); break;
                case 'n': append_byte('\n', output, output_size, &stored, &value_lossy); break;
                case 'r': append_byte('\r', output, output_size, &stored, &value_lossy); break;
                case 't': append_byte('\t', output, output_size, &stored, &value_lossy); break;
                case 'u': {
                    uint32_t codepoint = 0;
                    if (!parse_hex4(&codepoint)) return false;
                    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                        if (p_[0] != '\\' || p_[1] != 'u') return false;
                        p_ += 2;
                        uint32_t low = 0;
                        if (!parse_hex4(&low) || low < 0xdc00U || low > 0xdfffU) {
                            return false;
                        }
                        codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) +
                                    (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                        return false;
                    }
                    append_codepoint(codepoint, output, output_size, &stored, &value_lossy);
                    break;
                }
                default: return false;
            }
        }
        if (*p_ != '"') return false;
        ++p_;
        if (output != nullptr && output_size > 0) output[stored] = '\0';
        if (lossy != nullptr) *lossy = value_lossy;
        return true;
    }

    bool parse_u32(uint32_t* output) {
        skip_ws();
        if (p_ == nullptr || output == nullptr) return false;
        const char* q = p_;
        if (*q == '0') {
            ++q;
            if (is_digit(*q)) return false;
        } else if (*q >= '1' && *q <= '9') {
            ++q;
            while (is_digit(*q)) ++q;
        } else {
            return false;
        }
        if (*q == '.' || *q == 'e' || *q == 'E') return false;

        uint64_t value = 0;
        for (const char* digit = p_; digit < q; ++digit) {
            value = value * 10U + static_cast<unsigned>(*digit - '0');
            if (value > UINT32_MAX) return false;
        }
        p_ = q;
        *output = static_cast<uint32_t>(value);
        return true;
    }

    bool parse_number(float* output) {
        if (output == nullptr) return false;
        const char* start = nullptr;
        const char* finish = nullptr;
        if (!scan_number(&start, &finish)) return false;
        char* conversion_end = nullptr;
        const float value = std::strtof(start, &conversion_end);
        if (conversion_end != finish || !std::isfinite(value)) return false;
        *output = value;
        return true;
    }

    bool parse_bool(bool* output) {
        skip_ws();
        if (p_ == nullptr || output == nullptr) return false;
        if (std::strncmp(p_, "true", 4) == 0) {
            p_ += 4;
            *output = true;
            return true;
        }
        if (std::strncmp(p_, "false", 5) == 0) {
            p_ += 5;
            *output = false;
            return true;
        }
        return false;
    }

    bool parse_null() {
        skip_ws();
        if (p_ == nullptr || std::strncmp(p_, "null", 4) != 0) return false;
        p_ += 4;
        return true;
    }

    bool skip_value(unsigned depth = 0) {
        if (depth > 24U) return false;
        skip_ws();
        if (p_ == nullptr) return false;
        if (*p_ == '"') return parse_string(nullptr, 0);
        if (*p_ == '{') {
            ++p_;
            if (consume('}')) return true;
            while (true) {
                if (!parse_string(nullptr, 0) || !consume(':') ||
                    !skip_value(depth + 1U)) {
                    return false;
                }
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (*p_ == '[') {
            ++p_;
            if (consume(']')) return true;
            while (true) {
                if (!skip_value(depth + 1U)) return false;
                if (consume(']')) return true;
                if (!consume(',')) return false;
            }
        }
        if (std::strncmp(p_, "true", 4) == 0) { p_ += 4; return true; }
        if (std::strncmp(p_, "false", 5) == 0) { p_ += 5; return true; }
        if (std::strncmp(p_, "null", 4) == 0) { p_ += 4; return true; }
        const char* start = nullptr;
        const char* finish = nullptr;
        return scan_number(&start, &finish);
    }

private:
    static bool is_json_ws(char value) {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    }

    static bool is_digit(char value) { return value >= '0' && value <= '9'; }

    static int hex_value(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    bool parse_hex4(uint32_t* output) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4U; ++i) {
            if (*p_ == '\0') return false;
            const int digit = hex_value(*p_);
            if (digit < 0) return false;
            ++p_;
            value = (value << 4U) | static_cast<uint32_t>(digit);
        }
        *output = value;
        return true;
    }

    static void append_byte(unsigned char value,
                            char* output,
                            size_t output_size,
                            size_t* stored,
                            bool* lossy) {
        if (value == 0U) {
            *lossy = true;
            return;
        }
        if (output != nullptr && output_size > 0 && *stored + 1U < output_size) {
            output[(*stored)++] = static_cast<char>(value);
        } else {
            *lossy = true;
        }
    }

    static void append_codepoint(uint32_t codepoint,
                                 char* output,
                                 size_t output_size,
                                 size_t* stored,
                                 bool* lossy) {
        unsigned char bytes[4] = {};
        size_t count = 0;
        if (codepoint <= 0x7fU) {
            bytes[count++] = static_cast<unsigned char>(codepoint);
        } else if (codepoint <= 0x7ffU) {
            bytes[count++] = static_cast<unsigned char>(0xc0U | (codepoint >> 6U));
            bytes[count++] = static_cast<unsigned char>(0x80U | (codepoint & 0x3fU));
        } else if (codepoint <= 0xffffU) {
            bytes[count++] = static_cast<unsigned char>(0xe0U | (codepoint >> 12U));
            bytes[count++] = static_cast<unsigned char>(0x80U | ((codepoint >> 6U) & 0x3fU));
            bytes[count++] = static_cast<unsigned char>(0x80U | (codepoint & 0x3fU));
        } else {
            bytes[count++] = static_cast<unsigned char>(0xf0U | (codepoint >> 18U));
            bytes[count++] = static_cast<unsigned char>(0x80U | ((codepoint >> 12U) & 0x3fU));
            bytes[count++] = static_cast<unsigned char>(0x80U | ((codepoint >> 6U) & 0x3fU));
            bytes[count++] = static_cast<unsigned char>(0x80U | (codepoint & 0x3fU));
        }
        if (codepoint == 0U || output == nullptr || output_size == 0 ||
            *stored + count >= output_size) {
            *lossy = true;
            return;
        }
        for (size_t i = 0; i < count; ++i) output[(*stored)++] = static_cast<char>(bytes[i]);
    }

    bool scan_number(const char** start, const char** finish) {
        skip_ws();
        if (p_ == nullptr || start == nullptr || finish == nullptr) return false;
        const char* q = p_;
        *start = q;
        if (*q == '-') ++q;
        if (*q == '0') {
            ++q;
            if (is_digit(*q)) return false;
        } else if (*q >= '1' && *q <= '9') {
            ++q;
            while (is_digit(*q)) ++q;
        } else {
            return false;
        }
        if (*q == '.') {
            ++q;
            if (!is_digit(*q)) return false;
            while (is_digit(*q)) ++q;
        }
        if (*q == 'e' || *q == 'E') {
            ++q;
            if (*q == '+' || *q == '-') ++q;
            if (!is_digit(*q)) return false;
            while (is_digit(*q)) ++q;
        }
        p_ = q;
        *finish = q;
        return true;
    }

    const char* p_;
};
