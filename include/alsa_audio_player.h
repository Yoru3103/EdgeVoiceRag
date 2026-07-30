#pragma once

#include <memory>
#include <string>

#include "audio_player.h"

struct AlsaAudioPlayerConfig {
    /*
     * PC 通常使用 default。
     * 开发板可能使用 plughw:0,0、plughw:1,0 等。
     */
    std::string device = "default";

    /*
     * 每个 ALSA period 包含的音频帧数。
     * 较小的 period 打断延迟更低，但系统调用更频繁。
     */
    unsigned long period_frames = 1024;
};

class AlsaAudioPlayer final : public AudioPlayer {
public:
    explicit AlsaAudioPlayer(AlsaAudioPlayerConfig config = {});

    ~AlsaAudioPlayer() override;

    AlsaAudioPlayer(const AlsaAudioPlayer&) = delete;
    AlsaAudioPlayer& operator=(const AlsaAudioPlayer&) = delete;

    std::string name() const override;

    AudioPlaybackResult play(const AudioBuffer& audio) override;

    void stop() override;

    const AlsaAudioPlayerConfig& config() const;

private:
    class Impl;

    AlsaAudioPlayerConfig config_;
    std::unique_ptr<Impl> impl_;
};
