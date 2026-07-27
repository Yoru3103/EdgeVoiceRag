#pragma once

#include <stddef.h>
#include <cstdint>
#include <vector>

// TTS和播放器之间的通用数据
struct AudioBuffer {
    std::vector<std::int16_t> samples;

    int sample_rate = 16000;    // 采样率
    int channels = 1;           // 声道数

    bool empty() const {
        return samples.empty();
    }

    std::size_t frameCount() const {
        if (channels <= 0) {
            return 0;
        }

        return (
            samples.size() /
            static_cast<std::size_t>(channels)
        );
    }

    double durationSeconds() const {
        if (
            sample_rate <= 0 ||
            channels <= 0
        ) {
            return 0.0;
        }

        return (
            static_cast<double>(frameCount()) /
            static_cast<double>(sample_rate)
        );
    }
};
