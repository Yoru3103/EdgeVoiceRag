#include "alsa_audio_recorder.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <alsa/asoundlib.h>

namespace {

std::string alsaError(
    const std::string& operation,
    int error_code
) {
    return operation
        + ": "
        + snd_strerror(error_code);
}

void requireAlsaSuccess(
    int result,
    const std::string& operation
) {
    if (result < 0) {
        throw std::runtime_error(
            alsaError(operation, result)
        );
    }
}

void validateConfig(
    const AlsaAudioRecorderConfig& config
) {
    if (config.device.empty()) {
        throw std::invalid_argument(
            "ALSA capture device must not be empty"
        );
    }

    if (config.sample_rate <= 0) {
        throw std::invalid_argument(
            "ALSA capture sample rate must be "
            "greater than zero"
        );
    }

    if (config.channels <= 0) {
        throw std::invalid_argument(
            "ALSA capture channels must be "
            "greater than zero"
        );
    }

    if (config.record_duration_ms <= 0) {
        throw std::invalid_argument(
            "ALSA record duration must be "
            "greater than zero"
        );
    }

    if (config.period_frames == 0) {
        throw std::invalid_argument(
            "ALSA capture period_frames must "
            "be greater than zero"
        );
    }
}

std::size_t calculateTargetFrames(const AlsaAudioRecorderConfig& config) {
    const std::int64_t numerator = static_cast<std::int64_t>(config.sample_rate) *
        static_cast<std::int64_t>(config.record_duration_ms);

    const std::int64_t frames = numerator / 1000;   // 单位ms

    if (frames <= 0) {
        throw std::invalid_argument("ALSA capture duration produces no audio frames");
    }

    if (
        static_cast<std::uint64_t>(frames) > 
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) 
    ) {
        throw std::invalid_argument(
            "ALSA capture duration is too large"
        );
    }

    return static_cast<std::size_t>(frames);
}

}   // namespace

class AlsaAudioRecorder::Impl {
public:
    explicit Impl(const AlsaAudioRecorderConfig& config)
        : config_(config)
        , target_frames_(calculateTargetFrames(config)) {
        const int result = snd_pcm_open(
            &handle_,
            config.device.c_str(),
            SND_PCM_STREAM_CAPTURE,
            0
        );

        if (result < 0) {
            handle_ = nullptr;

            throw std::runtime_error(
                alsaError(
                    "failed to open ALSA capture device "
                        + config_.device,
                    result
                )
            );
        }

        try {
            configureDevice();
        } catch (...) {
            snd_pcm_close(handle_);
            handle_ = nullptr;

            throw;
        }
    }

    ~Impl() {
        stop();

        std::lock_guard<std::mutex> lock(record_mutex_);

        if (handle_ != nullptr) {
            snd_pcm_close(handle_);
            handle_ = nullptr;
        }
    }

    AudioCaptureResult recordUtterance() {
        const auto start = std::chrono::steady_clock::now();

        if (stopped_.load()) {
            return AudioCaptureResult::failure(
                "ALSA recorder is stopped"
            );
        }

        std::lock_guard<std::mutex> lock(record_mutex_);

        if (stopped_.load()) {
            return AudioCaptureResult::failure(
                "ALSA recorder is stopped"
            );
        }

        if (handle_ == nullptr) {
            return AudioCaptureResult::failure(
                "ALSA capture device is not initialized"
            );
        }

        const int prepare_result = snd_pcm_prepare(handle_);

        if (prepare_result < 0) {
            return AudioCaptureResult::failure(
                alsaError(
                    "failed to prepare ALSA capture",
                    prepare_result
                )
            );
        }

        const std::size_t channels = static_cast<std::size_t>(config_.channels);

        if (
            target_frames_
            > std::numeric_limits<std::size_t>::max() / channels
        ) {
            return AudioCaptureResult::failure("ALSA capture sample count overflows");
        }

        AudioBuffer audio;
        audio.sample_rate = config_.sample_rate;
        audio.channels = config_.channels;

        audio.samples.reserve(target_frames_ * channels);

        std::vector<std::int16_t> period_buffer(
            static_cast<std::size_t>(config_.period_frames) * channels
        );

        std::size_t captured_frames = 0;

        while (captured_frames < target_frames_) {
            if (stopped_.load()) {
                return AudioCaptureResult::failure("ALSA recording was interrupted");
            }

            const std::size_t remaining = 
                target_frames_ - captured_frames;

            const snd_pcm_uframes_t requested_frames = 
                static_cast<snd_pcm_uframes_t>(
                    std::min<std::size_t>(
                        remaining,
                        config_.period_frames
                    )
                );

            const snd_pcm_sframes_t result = 
                snd_pcm_readi(
                    handle_,
                    period_buffer.data(),
                    requested_frames
                );

            if (result == -EINTR) {
                continue;
            }

            if (result < 0) {
                if (stopped_.load()) {
                    return AudioCaptureResult::failure(
                        "ALSA recording was interrupted"
                    );
                }

                const int recovered = 
                    snd_pcm_recover(
                        handle_,
                        static_cast<int>(result),
                        1
                    );

                if (recovered < 0) {
                    return AudioCaptureResult::failure(
                        alsaError(
                            "failed to recover ALSA capture",
                            recovered
                        )
                    );
                }

                continue;
            }

            if (result == 0) {
                continue;
            }

            const std::size_t actual_frames = static_cast<std::size_t>(result);
            const std::size_t actual_samples = actual_frames * channels;

            audio.samples.insert(
                audio.samples.end(),
                period_buffer.begin(),
                period_buffer.begin() + static_cast<std::ptrdiff_t>(actual_samples)
            );

            captured_frames += actual_frames;
        }

        // 当次调用结束，丢弃多余采集数据
        snd_pcm_drop(handle_);

        const double elapsed_ms = 
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start
            ).count();

        return AudioCaptureResult::success(
            std::move(audio),
            elapsed_ms
        );
    }

    void stop() {
        /*
         * AudioRecorder::stop() 表示录音组件结束生命周期，
         * 与 AudioPlayer::stop() 的“仅打断当前播放”不同。
         */
        stopped_.store(true);

        if (handle_ != nullptr) {
            snd_pcm_drop(handle_);
        }
    }

private:
    void configureDevice() {
        snd_pcm_hw_params_t* params = nullptr;

        snd_pcm_hw_params_alloca(&params);

        // 初始化为设备支持的所有可能配置
        requireAlsaSuccess(
            snd_pcm_hw_params_any(
                handle_,
                params
            ),
            "failed to initialize capture parameters"
        );

        // 配置访问缓冲区方式（交错排列）
        requireAlsaSuccess(
            snd_pcm_hw_params_set_access(
                handle_,
                params,
                SND_PCM_ACCESS_RW_INTERLEAVED
            ),
            "failed to set interleaved capture"
        );

        // 有符号16位整数小端序
        requireAlsaSuccess(
            snd_pcm_hw_params_set_format(
                handle_,
                params,
                SND_PCM_FORMAT_S16_LE
            ),
            "failed to set S16_LE capture format"
        );

        // 配置通道数
        requireAlsaSuccess(
            snd_pcm_hw_params_set_channels(
                handle_,
                params,
                static_cast<unsigned int>(config_.channels)
            ),
            "failed to set S16_LE capture format"
        );

        // 采样率
        requireAlsaSuccess(
            snd_pcm_hw_params_set_rate(
                handle_,
                params,
                static_cast<unsigned int>(config_.sample_rate),
                0       // 要求等于给定采样率，-1可以选择不高于目标的值，1可以选择不低于目标的值
            ),
            "failed to set capture sample rate"
        );

        snd_pcm_uframes_t period_frames = 
            static_cast<snd_pcm_uframes_t>(config_.period_frames);

        // 设置一个period包含多少帧
        // near是因为声卡可能不支持精确的512帧，ALS会根据最接近的可用值，并将实际值写回
        requireAlsaSuccess(
            snd_pcm_hw_params_set_period_size_near(
                handle_,
                params,
                &period_frames,
                nullptr
            ),
            "failed to set capture period size"
        );

        // 提交参数给设备handle
        requireAlsaSuccess(
            snd_pcm_hw_params(
                handle_,
                params
            ),
            "failed to apply capture parameters"
        );

        requireAlsaSuccess(
            snd_pcm_prepare(handle_),
            "failed to prepare capture device"
        );
    }

    AlsaAudioRecorderConfig config_;
    std::size_t target_frames_;

    snd_pcm_t* handle_ = nullptr;

    std::atomic_bool stopped_{false};
    std::mutex record_mutex_;
};

AlsaAudioRecorder::AlsaAudioRecorder(AlsaAudioRecorderConfig config)
    : config_(std::move(config)) {
    validateConfig(config_);

    impl_ = std::make_unique<Impl>(config_);
}

AlsaAudioRecorder::~AlsaAudioRecorder() = default;

std::string AlsaAudioRecorder::name() const {
    return "alsa_capture";
}

AudioCaptureResult AlsaAudioRecorder::recordUtterance() {
    if (!impl_) {
        return AudioCaptureResult::failure("ALSA recorder is not initialized");
    }

    return impl_->recordUtterance();
}

void AlsaAudioRecorder::stop() {
    if (impl_) {
        impl_->stop();
    }
}

const AlsaAudioRecorderConfig& AlsaAudioRecorder::config() const {
    return config_;
}
