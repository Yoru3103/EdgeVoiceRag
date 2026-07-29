#include "wav_audio_recorder.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "wav_audio_reader.h"

WavAudioRecorder::WavAudioRecorder(std::string path)
    : path_(std::move(path)) {
    if (path_.empty()) {
        throw std::invalid_argument(
            "WAV recorder path must not "
            "be empty"
        );
    }
}

std::string WavAudioRecorder::name() const {
    return "wav_file";
}

AudioCaptureResult WavAudioRecorder::recordUtterance() {
    if (stopped_.load()) {
        return AudioCaptureResult::failure(
            "WAV recorder is stopped"
        );
    }

    const auto start = std::chrono::steady_clock::now();

    try {
        AudioBuffer audio = WavAudioReader::read(path_);

        if (stopped_.load()) {
            return AudioCaptureResult::failure(
                "WAV recording was interrupted"
            );
        }

        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start
        ).count();

        return AudioCaptureResult::success(
            std::move(audio),
            elapsed_ms
        );
    } catch (const std::exception& error) {
        return AudioCaptureResult::failure(
            "failed to read WAV input: " +
            std::string(error.what())
        );
    }
}

void WavAudioRecorder::stop() {
    stopped_.store(true);
}

const std::string& WavAudioRecorder::path() const {
    return path_;
}
