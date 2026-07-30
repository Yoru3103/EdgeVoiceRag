#include "sherpa_onnx_tts_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sherpa-onnx/c-api/c-api.h>

namespace {

namespace fs = std::filesystem;

struct OfflineTtsDeleter {
    void operator()(const SherpaOnnxOfflineTts* tts) const noexcept {
        if (tts != nullptr) {
            SherpaOnnxDestroyOfflineTts(tts);
        }
    } 
};

struct GeneratedAudioDeleter {
    void operator()(const SherpaOnnxGeneratedAudio* audio) const noexcept {
        if (audio != nullptr) {
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        }
    }
};

using OfflineTtsPtr = std::unique_ptr<
    const SherpaOnnxOfflineTts,
    OfflineTtsDeleter
>;

using GeneratedAudioPtr = std::unique_ptr<
    const SherpaOnnxGeneratedAudio,
    GeneratedAudioDeleter
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
            description + " path must not be empty"
        );
    }

    if (
        !fs::exists(path) || !fs::is_regular_file(path)
    ) {
        throw std::invalid_argument(
            description
                + " file does not exist: "
                + path
        );
    }
}

void validateConfig(const SherpaOnnxTtsConfig& config) {
    if (config.num_threads <= 0) {
        throw std::invalid_argument(
            "sherpa TTS num_threads must be "
            "greater than zero"
        );
    }

    if (config.speaker_id < 0) {
        throw std::invalid_argument(
            "sherpa TTS speaker_id must not "
            "be negative"
        );
    }

    if (config.max_num_sentences <= 0) {
        throw std::invalid_argument(
            "sherpa TTS max_num_sentences must "
            "be greater than zero"
        );
    }

    if (config.speed <= 0.0F) {
        throw std::invalid_argument(
            "sherpa TTS speed must be "
            "greater than zero"
        );
    }

    if (config.silence_scale < 0.0F) {
        throw std::invalid_argument(
            "sherpa TTS silence_scale must not "
            "be negative"
        );
    }

    if (config.provider.empty()) {
        throw std::invalid_argument(
            "sherpa TTS provider must not be empty"
        );
    }

    requireRegularFile(
        config.model_path,
        "VITS model"
    );

    requireRegularFile(
        config.lexicon_path,
        "VITS lexicon"
    );

    requireRegularFile(
        config.tokens_path,
        "VITS tokens"
    );

    for (const auto& rule_fst : config.rule_fsts) {
        requireRegularFile(
            rule_fst,
            "TTS rule FST"
        );
    }
}

std::string joinRuleFsts(const std::vector<std::string>& paths) {
    std::string result;

    for (std::size_t i = 0; i < paths.size(); i++) {
        if (i > 0) {
            result += ',';
        }

        result += paths[i];
    }

    return result;
}

std::int16_t floatToPcm16(float sample) {
    // 检查非法浮点数
    if (!std::isfinite(sample)) {
        throw std::runtime_error(
            "sherpa TTS returned a non-finite sample"
        );
    }

    // 将采样值限制到-1~1
    const float clamped = std::clamp(sample, -1.0F, 1.0F);

    /*
     * 正数最大映射到 32767；
     * 负数最小映射到 -32768。
     */
    const float scaled = clamped >= 0.0F ? clamped * 32767.0F : clamped * 32768.0F;

    // 四舍五入而非截断
    const long rounded = std::lround(scaled);

    return static_cast<std::int16_t>(
        std::clamp<long>(
            rounded,
            std::numeric_limits<std::int16_t>::min(),
            std::numeric_limits<std::int16_t>::max()
        )
    );
}

SherpaOnnxOfflineTtsConfig buildSherpaConfig(
    const SherpaOnnxTtsConfig& config,
    const std::string& rule_fsts
) {
    SherpaOnnxOfflineTtsConfig tts_config{};

    tts_config.model.vits.model = config.model_path.c_str();
    tts_config.model.vits.lexicon = config.lexicon_path.c_str();
    tts_config.model.vits.tokens = config.tokens_path.c_str();
    tts_config.model.vits.noise_scale = config.noise_scale;
    tts_config.model.vits.noise_scale_w = config.noise_scale_w;
    tts_config.model.vits.length_scale = config.length_scale;

    tts_config.model.num_threads = config.num_threads;
    tts_config.model.debug = config.debug ? 1 : 0;
    tts_config.model.provider = config.provider.c_str();

    tts_config.rule_fsts = rule_fsts.empty() ? nullptr : rule_fsts.c_str();

    tts_config.max_num_sentences = config.max_num_sentences;
    tts_config.silence_scale = config.silence_scale;

    return tts_config;
}

}   // namespace

class SherpaOnnxTtsBackend::Impl {
public:
    explicit Impl(const SherpaOnnxTtsConfig& config)
        : rule_fsts_(joinRuleFsts(config.rule_fsts))
        , tts_(createTts(config, rule_fsts_)) {
        const int speaker_count = SherpaOnnxOfflineTtsNumSpeakers(tts_.get());
        
        if (
            speaker_count > 0
            && config.speaker_id >= speaker_count
        ) {
            throw std::invalid_argument(
                "sherpa TTS speaker_id "
                + std::to_string(config.speaker_id)
                + " is outside supported range [0, "
                + std::to_string(speaker_count - 1)
                + "]"
            );
        }
    }

    TtsSynthesisResult synthesize(
        const std::string& text,
        const SherpaOnnxTtsConfig& config
    ) {
        const std::string normalized_text = trimAsciiWhitespace(text);

        if (normalized_text.empty()) {
            return TtsSynthesisResult::failure(
                "TTS input text must not be empty"
            );
        }

        try {
            // 当前voiceassistant只有一个TTS worker 但避免未来被并发调用，仍加锁
            std::lock_guard<std::mutex> lock(mutex_);

            SherpaOnnxGenerationConfig generation_config{};
            generation_config.sid = config.speaker_id;
            generation_config.speed = config.speed;
            generation_config.silence_scale = config.silence_scale;

            GeneratedAudioPtr generated_audio(
                SherpaOnnxOfflineTtsGenerateWithConfig(
                    tts_.get(),
                    normalized_text.c_str(),
                    &generation_config,
                    nullptr,
                    nullptr
                )
            );

            if (!generated_audio) {
                return TtsSynthesisResult::failure(
                    "sherpa TTS generation returned null"
                );
            }

            if (
                generated_audio->samples == nullptr ||
                generated_audio->n <= 0
            ) {
                return TtsSynthesisResult::failure(
                    "sherpa TTS returned empty audio"
                );
            }

            if (generated_audio->sample_rate <= 0) {
                return TtsSynthesisResult::failure(
                    "sherpa TTS returned invalid sample rate"
                );
            }

            AudioBuffer output;
            output.sample_rate = generated_audio->sample_rate;
            output.channels = 1;

            output.samples.reserve(
                static_cast<std::size_t>(generated_audio->n)
            );

            for (std::int32_t i = 0; i < generated_audio->n; i++) {
                output.samples.push_back(
                    floatToPcm16(generated_audio->samples[i])
                );
            }

            return TtsSynthesisResult::success(
                std::move(output)
            );
        } catch (const std::exception& error) {
            return TtsSynthesisResult::failure(
                "sherpa TTS synthesis failed: " + std::string(error.what())
            );
        }
    }

private:
    static OfflineTtsPtr createTts(
        const SherpaOnnxTtsConfig& config,
        const std::string& rule_fsts
    ) {
        const SherpaOnnxOfflineTtsConfig tts_config = buildSherpaConfig(
            config,
            rule_fsts
        );

        OfflineTtsPtr tts(SherpaOnnxCreateOfflineTts(&tts_config));

        if (!tts) {
            throw std::runtime_error(
                "failed to create sherpa-onnx "
                "offline TTS engine"
            );
        }

        return tts;
    }

    /*
     * rule_fsts_ 必须在创建 TTS 时保持有效，
     * 因为 C API 使用 const char*。
     */
    std::string rule_fsts_;
    OfflineTtsPtr tts_;
    std::mutex mutex_;
};

SherpaOnnxTtsBackend::SherpaOnnxTtsBackend(SherpaOnnxTtsConfig config)
    : config_(std::move(config)) {
    validateConfig(config_);

    impl_ = std::make_unique<Impl>(config_);
}

SherpaOnnxTtsBackend::~SherpaOnnxTtsBackend() = default;

std::string SherpaOnnxTtsBackend::name() const {
    return "sherpa_onnx_vits_tts";
}

TtsSynthesisResult SherpaOnnxTtsBackend::synthesize(const std::string& text) {
    if (!impl_) {
        return TtsSynthesisResult::failure(
            "sherpa TTS backend is not initialized"
        );
    }

    return impl_->synthesize(text, config_);
}

const SherpaOnnxTtsConfig& SherpaOnnxTtsBackend::config() const {
    return config_;
}
