#include "sherpa_onnx_asr_backend.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sherpa-onnx/c-api/c-api.h>

namespace {

namespace fs = std::filesystem;

struct OfflineRecognizerDeleter {
    void operator()(
        const SherpaOnnxOfflineRecognizer* recognizer
    ) const noexcept {
        if (recognizer != nullptr) {
            SherpaOnnxDestroyOfflineRecognizer(
                recognizer
            );
        }
    }
};

struct OfflineStreamDeleter {
    void operator()(
        const SherpaOnnxOfflineStream* stream
    ) const noexcept {
        if (stream != nullptr) {
            SherpaOnnxDestroyOfflineStream(stream);
        }
    }
};

struct OfflineResultDeleter {
    void operator()(
        const SherpaOnnxOfflineRecognizerResult* result
    ) const noexcept {
        if (result != nullptr) {
            SherpaOnnxDestroyOfflineRecognizerResult(
                result
            );
        }
    }
};

using OfflineRecognizerPtr = std::unique_ptr<
    const SherpaOnnxOfflineRecognizer,
    OfflineRecognizerDeleter
>;

using OfflineStreamPtr = std::unique_ptr<
    const SherpaOnnxOfflineStream,
    OfflineStreamDeleter
>;

using OfflineResultPtr = std::unique_ptr<
    const SherpaOnnxOfflineRecognizerResult,
    OfflineResultDeleter
>;

std::string trimAsciiWhitespace(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\n\r");

    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = text.find_last_not_of(" \t\n\r");

    return text.substr(first, last - first + 1);
}

void requireRegularFile(
    const std::string& path,
    const std::string& description
) {
    if (path.empty()) {
        throw std::invalid_argument(
            description +
            " path must not be empty"
        );
    }

    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        throw std::invalid_argument(
            description +
            " file does not exist: " +
            path
        );
    }
}

void validateConfig(const SherpaOnnxAsrConfig& config) {
    if (config.num_threads <= 0) {
        throw std::invalid_argument(
            "sherpa ASR num_threads must "
            "be greater than zero"
        );
    }

    if (config.language.empty()) {
        throw std::invalid_argument(
            "sherpa ASR language must not "
            "be empty"
        );
    }

    if (config.provider.empty()) {
        throw std::invalid_argument(
            "sherpa ASR provider must not "
            "be empty"
        );
    }

    requireRegularFile(
        config.model_path,
        "SenseVoice model"
    );

    requireRegularFile(
        config.tokens_path,
        "SenseVoice tokens"
    );
}

SherpaOnnxOfflineRecognizerConfig buildConfig(
    const SherpaOnnxAsrConfig& config
) {
    SherpaOnnxOfflineRecognizerConfig recognizer_config{};

    recognizer_config.feat_config.sample_rate = 16000;
    recognizer_config.feat_config.feature_dim = 80;

    recognizer_config.model_config.sense_voice.model =
        config.model_path.c_str();
    recognizer_config.model_config.sense_voice.language =
        config.language.c_str();
    recognizer_config.model_config.sense_voice.use_itn =
        config.use_itn ? 1 : 0;
    recognizer_config.model_config.tokens =
        config.tokens_path.c_str();
    recognizer_config.model_config.num_threads = config.num_threads;
    recognizer_config.model_config.provider =
        config.provider.c_str();
    recognizer_config.model_config.debug =
        config.debug ? 1 : 0;

    recognizer_config.decoding_method = "greedy_search";

    return recognizer_config;
}

std::vector<float> convertToMonoFloat(const AudioBuffer& audio) {
    if (audio.empty()) {
        throw std::invalid_argument(
            "ASR audio samples are empty"
        );
    }

    if (audio.sample_rate != 16000) {
        throw std::invalid_argument(
            "SenseVoice ASR requires "
            "16000 Hz audio"
        );
    }

    if (audio.channels <= 0) {
        throw std::invalid_argument(
            "ASR audio channels must be "
            "greater than zero"
        );
    }

    const std::size_t channels = static_cast<std::size_t>(audio.channels);

    if (audio.samples.size() % channels != 0) {
        throw std::invalid_argument(
            "ASR audio sample count is not "
            "aligned to channels"
        );
    }

    const std::size_t frames = audio.samples.size() / channels;

    if (frames > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument(
            "ASR audio contains too many "
            "samples"
        );
    }

    std::vector<float> samples;

    samples.reserve(frames);

    for (std::size_t frame = 0; frame < frames; frame++) {
        std::int64_t sum = 0;
        for (std::size_t channel = 0; channel < channels; channel++) {
            sum += audio.samples[frame * channels + channel];
        }

        const float mono_sample = static_cast<float>(sum) /
            static_cast<float>(channels) /
            32768.0F;

        samples.push_back(mono_sample);
    }

    return samples;
}

}   // namespace

class SherpaOnnxAsrBackend::Impl {
public:
    explicit Impl(const SherpaOnnxAsrConfig& config)
        : recognizer_(createRecognizer(config)) {
    }

    AsrTranscriptionResult transcribe(const AudioBuffer& audio) {
        const auto start = std::chrono::steady_clock::now();

        try {
            std::vector<float> samples = convertToMonoFloat(audio);

            std::lock_guard<std::mutex> lock(decode_mutex_);

            OfflineStreamPtr stream(
                SherpaOnnxCreateOfflineStream(
                    recognizer_.get()
                )
            );

            if (!stream) {
                return AsrTranscriptionResult::failure("failed to create sherpa offline stream");
            }

            SherpaOnnxAcceptWaveformOffline(
                stream.get(),
                audio.sample_rate,
                samples.data(),
                static_cast<std::int32_t>(samples.size())
            );

            SherpaOnnxDecodeOfflineStream(
                recognizer_.get(),
                stream.get()
            );

            OfflineResultPtr recognizer_result(
                SherpaOnnxGetOfflineStreamResult(
                    stream.get()
                )
            );

            if (!recognizer_result) {
                return AsrTranscriptionResult::failure(
                    "failed to get sherpa offline result"
                );
            }

            const std::string text = trimAsciiWhitespace(
                recognizer_result->text != nullptr
                    ? recognizer_result->text
                    : ""
            );

            const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

            if (text.empty()) {
                return AsrTranscriptionResult::failure(
                    "sherpa ASR returned "
                    "empty text"
                );
            }

            return AsrTranscriptionResult::success(
                text,
                elapsed_ms
            );
        } catch (std::exception& error) {
            return AsrTranscriptionResult::failure(
                "sherpa ASR inference failed: " + std::string(error.what())
            );
        }
    }

private:
    static OfflineRecognizerPtr createRecognizer(
        const SherpaOnnxAsrConfig& config
    ) {
        const SherpaOnnxOfflineRecognizerConfig recognizer_config =
            buildConfig(config);

        OfflineRecognizerPtr recognizer(
            SherpaOnnxCreateOfflineRecognizer(
                &recognizer_config
            )
        );

        if (!recognizer) {
            throw std::runtime_error(
                "failed to create sherpa "
                "offline recognizer"
            );
        }

        return recognizer;
    }

    OfflineRecognizerPtr recognizer_;
    std::mutex decode_mutex_;
};

SherpaOnnxAsrBackend::SherpaOnnxAsrBackend(SherpaOnnxAsrConfig config)
    : config_(std::move(config)) {
    validateConfig(config_);

    impl_ = std::make_unique<Impl>(config_);
}

SherpaOnnxAsrBackend::~SherpaOnnxAsrBackend() = default;

std::string SherpaOnnxAsrBackend::name() const {
    return "sherpa_onnx_sense_voice";
}

AsrTranscriptionResult SherpaOnnxAsrBackend::transcribe(const AudioBuffer& audio) {
    return impl_->transcribe(audio);
}

const SherpaOnnxAsrConfig& SherpaOnnxAsrBackend::config() const {
    return config_;
}
