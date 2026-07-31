#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "audio_buffer.h"

struct PcmStreamResult {
    bool ok =false;
    std::size_t captured_frames = 0;
    std::string error;

    static PcmStreamResult success(std::size_t captured_frames) {
        PcmStreamResult result;

        result.ok = true;
        result.captured_frames = captured_frames;

        return result;
    }

    static PcmStreamResult failure(const std::string& error) {
        PcmStreamResult result;

        result.ok = false;
        result.error = error;

        return result;
    }
};

/*
 * 返回 true：继续采集。
 * 返回 false：当前采集会话结束。
 */
using PcmChunkHandler = std::function<bool(const AudioBuffer&)>;

// Alsa和VAD之间的接口，防止VAD和ALSA强耦合，测试必须依赖麦克风
class PcmStreamSource {
public:
    virtual ~PcmStreamSource() = default;

    virtual std::string name() const = 0;

    virtual int sampleRate() const = 0;
    virtual int channels() const = 0;

    /*
     * 连续读取 PCM 并调用 handler。
     * handler 返回 false 时正常结束本次 capture。
     */
    virtual PcmStreamResult capture(const PcmChunkHandler& handler) = 0;

    virtual void stop() = 0;
};
