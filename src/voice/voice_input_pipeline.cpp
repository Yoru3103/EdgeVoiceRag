#include "voice_input_pipeline.h"

#include <exception>
#include <string>

VoiceInputPipelineResult VoiceInputPipelineResult::failure(
    const std::string& request_id,
    const std::string& error
) {
    VoiceInputPipelineResult result;

    result.ok = false;
    result.request_id = request_id;
    result.error = error;

    return result;
}

VoiceInputPipeline::VoiceInputPipeline(
    AudioRecorder& recorder,
    AsrBackend& asr_backend,
    VoiceAssistant& assistant
)
    : recorder_(recorder)
    , asr_backend_(asr_backend)
    , assistant_(assistant) {
}

VoiceInputPipelineResult VoiceInputPipeline::processOnce(const std::string& request_id) {
    if (request_id.empty()) {
        return VoiceInputPipelineResult::failure(
            request_id,
            "request_id must not be empty"
        );
    }

    VoiceInputPipelineResult result;

    result.request_id = request_id;
    result.recorder_backend = recorder_.name();
    result.asr_backend = asr_backend_.name();

    try {
        AudioCaptureResult capture = recorder_.recordUtterance();

        result.capture_elapsed_ms = capture.elapsed_ms;

        if (!capture.ok) {
            result.error = "audio capture failed: " + capture.error;

            return result;
        }

        std::string audio_error;

        if (!validateAudio(capture.audio, audio_error)) {
            result.error = "invalid captured audio: " + audio_error;

            return result;
        }

        const AsrTranscriptionResult transcription = asr_backend_.transcribe(capture.audio);

        result.asr_elapsed_ms = transcription.elapsed_ms;

        if (!transcription.ok) {
            result.error = "ASR failed: " + transcription.error;

            return result;
        }

        if (transcription.text.empty()) {
            result.error = "ASR returned empty text";

            return result;
        }

        result.recognized_text = transcription.text;

        result.assistant_result = assistant_.processText(
            request_id,
            transcription.text
        );

        if (!result.assistant_result.ok) {
            result.error =
                "voice assistant failed: " +
                result.assistant_result.error;

            return result;
        }
    } catch (const std::exception& error) {
        result.ok = false;

        result.error = "voice input pipeline failed: " + std::string(error.what());

        return result;
    }

    result.ok = true;
    return result;
}

void VoiceInputPipeline::stopRecording() {
    recorder_.stop();
}

bool VoiceInputPipeline::validateAudio(
    const AudioBuffer& audio,
    std::string& error
) {
    if(audio.empty()) {
        error = "audio samples are empty";
        return false;
    }

    if (audio.sample_rate <= 0) {
        error =
            "sample_rate must be greater than zero";

        return false;
    }

    if (audio.channels <= 0) {
        error =
            "channels must be greater than zero";

        return false;
    }

    const auto channel_count = static_cast<std::size_t>(audio.channels);

    if (audio.samples.size() % channel_count != 0) {
        error = "sample count is not divisible by channel count";

        return false;
    }

    error.clear();

    return true;
}
