#pragma once

#include <string>

#include "audio_buffer.h"

struct AudioPlaybackResult {
    bool ok = false;
    std::string error;

    static AudioPlaybackResult success() {
        AudioPlaybackResult result;
        result.ok = true;

        return result;
    }

    static AudioPlaybackResult failure(
        const std::string& error
    ) {
        AudioPlaybackResult result;

        result.ok = false;
        result.error = error;

        return result;
    }
};

class AudioPlayer {
public:
    virtual ~AudioPlayer() = default;

    virtual std::string name() const = 0;

    virtual AudioPlaybackResult play(const AudioBuffer& aduio) = 0;

    virtual void stop() = 0;
};
