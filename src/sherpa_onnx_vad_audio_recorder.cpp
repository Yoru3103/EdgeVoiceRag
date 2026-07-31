#include "sherpa_onnx_vad_audio_recorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
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

struct VadDeleter {
    void operator()(
        const SherpaOnnxVoiceActivityDetector* vad
    ) const noexcept {
        if (vad != nullptr) {
            SherpaOnnxDestroyVoiceActivityDetector(vad);
        }
    }
};

struct SpeechSegmentDeleter {
    void operator()(    // operaor：函数调用运算符重载，使一个对象可以像一个函数调用
        const SherpaOnnxSpeechSegment* segment
    ) const noexcept {  // 调用删除器时不删除删除器对象本身，noexcept保证释放资源时不抛异常
        if (segment != nullptr) {
            SherpaOnnxDestroySpeechSegment(segment);
        }
    }
};

using VadPtr = std::unique_ptr<
    const SherpaOnnxVoiceActivityDetector,
    VadDeleter
>;

using SpeechSegmentPtr = std::unique_ptr<
    const SherpaOnnxSpeechSegment,
    SpeechSegmentDeleter
>;

void validateConfig(const SherpaOnnxVadConfig& config) {
    if (
        config.model_path.empty()
        || !fs::exists(config.model_path)
        || !fs::is_regular_file(config.model_path)
    ) {
        throw std::invalid_argument(
            "Silero VAD model does not exist: "
                + config.model_path
        );
    }

    if (config.sample_rate != 16000) {
        throw std::invalid_argument(
            "Silero VAD requires 16000 Hz audio"
        );
    }

    if (config.num_threads <= 0) {
        throw std::invalid_argument(
            "VAD num_threads must be greater "
            "than zero"
        );
    }

    if (config.window_size <= 0) {
        throw std::invalid_argument(
            "VAD window_size must be greater "
            "than zero"
        );
    }

    if (
        config.threshold <= 0.0F
        || config.threshold >= 1.0F
    ) {
        throw std::invalid_argument(
            "VAD threshold must be between "
            "zero and one"
        );
    }

    if (config.min_silence_duration <= 0.0F) {
        throw std::invalid_argument(
            "VAD minimum silence duration must "
            "be greater than zero"
        );
    }

    if (config.min_speech_duration <= 0.0F) {
        throw std::invalid_argument(
            "VAD minimum speech duration must "
            "be greater than zero"
        );
    }

    if (config.max_speech_duration <= 0.0F) {
        throw std::invalid_argument(
            "VAD maximum speech duration must "
            "be greater than zero"
        );
    }

    if (config.max_wait_seconds <= 0.0F) {
        throw std::invalid_argument(
            "VAD maximum wait time must be "
            "greater than zero"
        );
    }

    if (config.buffer_size_seconds <= 0.0F) {
        throw std::invalid_argument(
            "VAD buffer size must be greater "
            "than zero"
        );
    }

    if (config.provider.empty()) {
        throw std::invalid_argument(
            "VAD provider must not be empty"
        );
    }
}

SherpaOnnxVadModelConfig buildVadConfig(const SherpaOnnxVadConfig& config) {
    SherpaOnnxVadModelConfig vad_config {};

    vad_config.silero_vad.model = config.model_path.c_str();
    vad_config.silero_vad.threshold = config.threshold;
    vad_config.silero_vad.min_silence_duration = config.min_silence_duration;
    vad_config.silero_vad.min_speech_duration = config.min_speech_duration;
    vad_config.silero_vad.max_speech_duration = config.max_speech_duration;
    vad_config.silero_vad.window_size = config.window_size;
    vad_config.sample_rate = config.sample_rate;
    vad_config.num_threads = config.num_threads;
    vad_config.provider = config.provider.c_str();
    vad_config.debug = config.debug ? 1 : 0;

    return vad_config;
}

std::vector<float> convertToMonoFloat(const AudioBuffer& chunk) {
    if (chunk.empty()) {
        throw std::invalid_argument("VAD PCM chunk must not be empty");
    }

    if (chunk.sample_rate != 16000) {
        throw std::invalid_argument("VAD PCM chunk must use 16000 Hz");
    }

    if (chunk.channels != 1) {
        throw std::invalid_argument(
            "VAD PCM chunk must be mono"
        );
    }

    std::vector<float> samples;
    samples.reserve(chunk.samples.size());

    for (const std::int16_t sample : chunk.samples) {
        samples.push_back(
            static_cast<float>(sample) / 32768.0F   // 归一
        );
    }

    return samples;
}

std::int16_t floatToPcm16(float sample) {
    if (!std::isfinite(sample)) {
        throw std::runtime_error(
            "VAD returned non-finite audio"
        );
    }

    const float clamped = std::clamp(sample, -1.0F, 1.0F);

    const float scaled = clamped >= 0.0F ? clamped * 32767.0F : clamped * 32768.0F;

    return static_cast<std::int16_t>(
        std::clamp<long>(
            std::lround(scaled),
            std::numeric_limits<std::int16_t>::min(),
            std::numeric_limits<std::int16_t>::max()
        )
    );
}

}   // namespace

class SherpaOnnxVadAudioRecorder::Impl {
public:
    Impl(
        PcmStreamSource& source,
        const SherpaOnnxVadConfig& config,
        std::atomic_bool& stopped
    )
        : source_(source)
        , config_(config)
        , stopped_(stopped)
        , vad_(createVad(config)) {}

    AudioCaptureResult recordUtterance() {
        std::lock_guard<std::mutex> lock(record_mutex_);

        if (stopped_.load()) {
            return AudioCaptureResult::failure("VAD recorder is stopped");
        }

        SherpaOnnxVoiceActivityDetectorReset(vad_.get());

        const auto start = std::chrono::steady_clock::now();

        bool timed_out = false;
        bool speech_started = false;

        std::size_t received_frames = 0;

        AudioBuffer utterance;
        utterance.sample_rate = config_.sample_rate;
        utterance.channels = 1;

        const auto consumeFrontSegment = 
            [this, &utterance]() -> bool {
                if (SherpaOnnxVoiceActivityDetectorEmpty(vad_.get())) {
                    return false;
                }

                SpeechSegmentPtr segment(
                    SherpaOnnxVoiceActivityDetectorFront(vad_.get())
                );

                if (
                    !segment
                    || segment->samples == nullptr
                    || segment->n <= 0
                ) {
                    if (segment) {
                        SherpaOnnxVoiceActivityDetectorPop(vad_.get());
                    }

                    return false;
                }

                utterance.samples.reserve(static_cast<std::size_t>(segment->n));

                for (std::int32_t i = 0; i < segment->n; i++) {
                    utterance.samples.push_back(
                        floatToPcm16(segment->samples[i])
                    );
                }

                SherpaOnnxVoiceActivityDetectorPop(vad_.get());

                return true;
            };

        const PcmStreamResult stream_result = 
            source_.capture(
                [
                    this,
                    &timed_out,
                    &speech_started,
                    &received_frames,
                    &consumeFrontSegment
                ](const AudioBuffer& chunk) {
                    if (stopped_.load()) {
                        return false;
                    }

                    const std::vector<float> samples = convertToMonoFloat(chunk);
                    
                    received_frames += chunk.frameCount();

                    SherpaOnnxVoiceActivityDetectorAcceptWaveform(
                        vad_.get(),
                        samples.data(),
                        static_cast<std::int32_t>(samples.size())
                    );

                    if (
                        SherpaOnnxVoiceActivityDetectorDetected(vad_.get()) 
                    ) {
                        speech_started = true;
                    }

                    if (consumeFrontSegment()) {
                        return false;
                    }

                    /*
                     * 使用已经收到的音频长度判断超时，
                     * 而不是墙上时钟。这样测试不依赖机器速度。
                     */
                    const double received_seconds = 
                        static_cast<double>(received_frames) / static_cast<double>(config_.sample_rate);
                    
                    if (!speech_started && received_seconds >= config_.max_wait_seconds) {
                        timed_out = true;
                        return false;
                    }

                    return true;
                }
            );
        
        if (stopped_.load()) {
            return AudioCaptureResult::failure("VAD recording was interrupted");
        }

        if (!stream_result.ok) {
            return AudioCaptureResult::failure(
                "PCM stream failed: "
                    + stream_result.error
            );
        }

        if (timed_out) {
            return AudioCaptureResult::failure(
                 "no speech detected within "
                    + std::to_string(
                        config_.max_wait_seconds
                    )
                    + " seconds"
            );
        }

        /*
         * 文件或有限 Mock PCM 源结束时，强制处理尾部。
         * 真实 ALSA 流通常会在检测到尾部静音时提前结束。
         */
        if (utterance.empty()) {
            SherpaOnnxVoiceActivityDetectorFlush(vad_.get());

            consumeFrontSegment();
        }

        if (utterance.empty()) {
            return AudioCaptureResult::failure(
                "VAD returned no complete "
                "speech segment"
            );
        }

        const double elapsed_ms = 
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()
                    - start
            ).count();
        
        return AudioCaptureResult::success(
            std::move(utterance),
            elapsed_ms
        );
    }

private:
    static VadPtr createVad(const SherpaOnnxVadConfig& config) {
        const SherpaOnnxVadModelConfig vad_config = buildVadConfig(config);

        VadPtr vad(
            SherpaOnnxCreateVoiceActivityDetector(
                &vad_config,
                config.buffer_size_seconds
            )
        );

        if (!vad) {
            throw std::runtime_error("failed to create sherpa-onnx VAD");
        }

        return vad;
    }

    PcmStreamSource& source_;
    SherpaOnnxVadConfig config_;
    std::atomic_bool& stopped_;

    VadPtr vad_;
    std::mutex record_mutex_;
};

SherpaOnnxVadAudioRecorder::SherpaOnnxVadAudioRecorder(
    PcmStreamSource& source,
    SherpaOnnxVadConfig config
)
    : source_(source)
    , config_(config) {
    validateConfig(config_);

    if (source_.sampleRate() != config_.sample_rate) {
        throw std::invalid_argument(
            "PCM source and VAD sample rates "
            "do not match"
        );
    }

    if (source_.channels() != 1) {
        throw std::invalid_argument(
            "Silero VAD PCM source must be mono"
        );
    }

    impl_ = std::make_unique<Impl>(
        source_,
        config_,
        stopped_
    );
}

SherpaOnnxVadAudioRecorder::~SherpaOnnxVadAudioRecorder() {
    stop();
}

std::string SherpaOnnxVadAudioRecorder::name() const {
    return "sherpa_onnx_silero_vad";
}

AudioCaptureResult SherpaOnnxVadAudioRecorder::recordUtterance() {
    if (!impl_) {
        return AudioCaptureResult::failure(
            "VAD recorder is not initialized"
        );
    }

    return impl_->recordUtterance();
}

void SherpaOnnxVadAudioRecorder::stop() {
    stopped_.store(true);
    source_.stop();
}

const SherpaOnnxVadConfig& SherpaOnnxVadAudioRecorder::config() const {
    return config_;
}
