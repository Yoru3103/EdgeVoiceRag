#pragma once

#include <atomic>
#include <string>

#include "audio_recorder.h"

class WavAudioRecorder final
    :public AudioRecorder {
public:
    explicit WavAudioRecorder(std::string path);

    std::string name() const override;

    AudioCaptureResult recordUtterance() override;

    void stop() override;

    const std::string& path() const;

private:
    std::string path_;

    std::atomic_bool stopped_{false};
};
