#pragma once

#include <memory>
#include <string>

#include "pcm_stream_source.h"

struct AlsaPcmStreamSourceConfig {
    std::string device = "default";

    int sample_rate = 16000;
    int channels = 1;

    /*
     * Silero VAD 的标准窗口是 512 个采样点。
     * 16000 Hz 下对应 32 ms。
     */
    unsigned long chunk_frames = 512;

     // ALSA缓冲延迟，单位微秒
     unsigned int latency_us = 100000;
};

class AlsaPcmStreamSource final : public PcmStreamSource {
public:
    explicit AlsaPcmStreamSource(AlsaPcmStreamSourceConfig config = {});

    ~AlsaPcmStreamSource() override;

    // RAII，同一资源只能有一个明确的所有者
    AlsaPcmStreamSource(const AlsaPcmStreamSource&) = delete;
    AlsaPcmStreamSource& operator=(const AlsaPcmStreamSource&) = delete;

    std::string name() const override;

    int sampleRate() const override;
    int channels() const override;

    PcmStreamResult capture(const PcmChunkHandler& handler) override;

    void cancelCurrentCapture() override;

    void stop() override;

    const AlsaPcmStreamSourceConfig& config() const;

private:
    class Impl;

    AlsaPcmStreamSourceConfig config_;
    std::unique_ptr<Impl> impl_;
};
