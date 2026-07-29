#pragma once

#include <string>

#include "audio_buffer.h"

struct AsrTranscriptionResult {
    bool ok = false;

    std::string text;
    std::string error;

    double elapsed_ms = 0.0;

    static AsrTranscriptionResult success(
        const std::string& text,
        double elapsed_ms = 0.0
    ) {
        AsrTranscriptionResult result;

        result.ok = true;
        result.text = text;
        result.elapsed_ms = elapsed_ms;

        return result;
    }

    static AsrTranscriptionResult failure(
        const std::string& error
    ) {
        AsrTranscriptionResult result;

        result.ok = false;
        result.error = error;

        return result;
    }
};

class AsrBackend {
public:
    virtual ~AsrBackend() = default;

    virtual std::string name() const = 0;

    virtual AsrTranscriptionResult transcribe(const AudioBuffer& audio) = 0;
};
