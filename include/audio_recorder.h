#pragma once

#include <string>
#include <utility>

#include "audio_buffer.h"

struct AudioCaptureResult {
    bool ok = false;

    AudioBuffer audio;
    std::string error;

    double elapsed_ms = 0.0;

    static AudioCaptureResult success(
        AudioBuffer audio,
        double elapsed_ms = 0.0
    ) {
        AudioCaptureResult result;
    
        result.ok = true;
        result.audio = std::move(audio);
        result.elapsed_ms = elapsed_ms;

        return result;
    }

    static AudioCaptureResult failure(
        const std::string& error
    ) {
        AudioCaptureResult result;

        result.ok = false;
        result.error = error;

        return result;
    }
};

class AudioRecorder {
public:
    virtual ~AudioRecorder() = default;

    virtual std::string name() const = 0;

    /*
     * 录制一段完整用户语音。
     *
     * Mock 可以直接返回预设 PCM；
     * 板端实现将通过 ALSA + VAD 判断语音结束。
     */
    virtual AudioCaptureResult recordUtterance() = 0;   // 返回一段完整的句子，而不是任意长度PCM块

    virtual void stop() = 0;
};
