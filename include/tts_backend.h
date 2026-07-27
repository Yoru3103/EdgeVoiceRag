#pragma once

#include <string>
#include <utility>

#include "audio_buffer.h"

struct TtsSynthesisResult {
    bool ok = false;

    AudioBuffer audio;
    std::string error;

    static TtsSynthesisResult success(AudioBuffer audio) {
        TtsSynthesisResult result;

        result.ok = true;
        result.audio = std::move(audio);

        return result;
    }

    static TtsSynthesisResult failure(const std::string& error) {
        TtsSynthesisResult result;
        result.ok = false;
        result.error = error;

        return result;
    }
};

// 将输出文字转换为音频数据
class TtsBackend {
public:
    virtual ~TtsBackend() = default;

    virtual std::string name() const = 0;

    virtual TtsSynthesisResult synthesize(const std::string& text) = 0;
};
