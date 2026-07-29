#pragma once

#include <string>

#include "asr_backend.h"
#include "audio_recorder.h"
#include "voice_assistant.h"

struct VoiceInputPipelineResult {
    bool ok = false;

    std::string request_id;
    std::string recognized_text;
    std::string error;

    std::string recorder_backend;
    std::string asr_backend;

    double capture_elapsed_ms = 0.0;
    double asr_elapsed_ms = 0.0;

    VoiceAssistantResult assistant_result;

    static VoiceInputPipelineResult failure(
        const std::string& request_id,
        const std::string& error
    );
};

class VoiceInputPipeline {
public:
    VoiceInputPipeline(
        AudioRecorder& recorder,
        AsrBackend& asr_backend,
        VoiceAssistant& assistant
    );

    VoiceInputPipelineResult processOnce(
        const std::string& request_id
    );

    void stopRecording();

private:
    AudioRecorder& recorder_;
    AsrBackend& asr_backend_;
    VoiceAssistant& assistant_;

    static bool validateAudio(
        const AudioBuffer& audio,
        std::string& error
    );
};
