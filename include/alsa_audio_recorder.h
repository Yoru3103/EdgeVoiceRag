#pragma once

#include <memory>
#include <string>

#include "audio_recorder.h"

struct AlsaAudioRecorderConfig {
    std::string device = "default";

    int sample_rate = 16000;
    int channels = 1;

    int record_duration_ms = 5000;

    unsigned long period_frames = 512;
};

class AlsaAudioRecorder final : public AudioRecorder {
public:
    explicit AlsaAudioRecorder(AlsaAudioRecorderConfig config = {});

    ~AlsaAudioRecorder() override;

    AlsaAudioRecorder(const AlsaAudioRecorder&) = delete;
    AlsaAudioRecorder& operator=(const AlsaAudioRecorder&) = delete;

    std::string name() const override;

    AudioCaptureResult recordUtterance() override;

    void stop() override;

    const AlsaAudioRecorderConfig& config() const;

private:
    class Impl;

    AlsaAudioRecorderConfig config_;
    std::unique_ptr<Impl> impl_;
};
