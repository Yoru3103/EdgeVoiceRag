#include "alsa_pcm_stream_source.h"

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

void validateConfig(const AlsaPcmStreamSourceConfig& config) {
    if (config.device.empty()) {
        throw std::invalid_argument(
            "ALSA stream device must not be empty"
        );
    }

    if (config.sample_rate <= 0) {
        throw std::invalid_argument(
            "ALSA stream sample rate must be "
            "greater than zero"
        );
    }

    if (config.channels <= 0) {
        throw std::invalid_argument(
            "ALSA stream channels must be "
            "greater than zero"
        );
    }

    if (config.chunk_frames == 0) {
        throw std::invalid_argument(
            "ALSA stream chunk_frames must be "
            "greater than zero"
        );
    }

    if (config.latency_us == 0) {
        throw std::invalid_argument(
            "ALSA stream latency must be "
            "greater than zero"
        );
    }

    const std::size_t frames =
        static_cast<std::size_t>(
            config.chunk_frames
        );

    const std::size_t channels =
        static_cast<std::size_t>(
            config.channels
        );

    if (
        frames
        > std::numeric_limits<std::size_t>::max()
            / channels
    ) {
        throw std::invalid_argument(
            "ALSA stream chunk size overflows"
        );
    }
}

}   // namespace

class AlsaPcmStreamSource::Impl {
public:
    explicit Impl(const AlsaPcmStreamSourceConfig& config)
        : config_(config) {
        const int open_result = snd_pcm_open(
            &handle_,
            config_.device.c_str(),
            SND_PCM_STREAM_CAPTURE, // 捕获音频
            0                       // 阻塞模式
        );

        if (open_result < 0) {
            handle_ = nullptr;
            
            throw std::runtime_error(
                alsaError(
                    "failed to open ALSA stream device "
                        + config_.device,
                    open_result
                )
            );
        }

        // 配置录音格式
        const int configure_result = 
            snd_pcm_set_params(
                handle_,
                SND_PCM_FORMAT_S16_LE,
                SND_PCM_ACCESS_RW_INTERLEAVED,  // 通道交错排布
                static_cast<unsigned int>(config_.channels),
                static_cast<unsigned int>(config_.sample_rate),
                1,  // 允许ALSA插件进行采样率转换
                config_.latency_us  //目标延迟
            );

        if (configure_result < 0) {
            snd_pcm_close(handle_);
            handle_ = nullptr;

            throw std::runtime_error(
                alsaError(
                    "failed to configure ALSA "
                    "stream device",
                    configure_result
                )
            );
        }
    }

    ~Impl() {
        stop();

        std::lock_guard<std::mutex> lock(capture_mutex_);

        if (handle_ != nullptr) {
            snd_pcm_close(handle_);
            handle_ = nullptr;
        }
    }

    PcmStreamResult capture(const PcmChunkHandler& handler) {
        if (!handler) {
            return PcmStreamResult::failure("PCM chunk handler must not be empty");
        } 

        // 加锁前快速检查
        if (stopped.load()) {
            return PcmStreamResult::failure("ALSA PCM stream source is stopped");
        }

        std::lock_guard<std::mutex> lock(capture_mutex_);

        // 加锁后二次确认
        if (stopped.load()) {
            return PcmStreamResult::failure("ALSA PCM stream source is stopped");
        }

        capture_cancelled_.store(false);

        if (handle_ == nullptr) {
            return PcmStreamResult::failure("ALSA PCM stream is not initialized");
        }

        // 将设备置于可读取状态；snd_pcm_drop则是不可读取
        const int prepare_result = snd_pcm_prepare(handle_);

        if (prepare_result < 0) {
            return PcmStreamResult::failure(
                alsaError(
                    "failed to prepare ALSA PCM stream",
                    prepare_result
                )
            );
        }

        const std::size_t channels = 
            static_cast<std::size_t>(config_.channels);
        const std::size_t chunk_frames = 
            static_cast<std::size_t>(config_.chunk_frames);

        // 创建接收缓冲区
        std::vector<std::int16_t> samples(chunk_frames * channels);

        std::size_t total_frames = 0;

        try {
            while (!stopped.load() && !capture_cancelled_.load()) {
                // 从麦克风读取
                const snd_pcm_sframes_t read_result = 
                    snd_pcm_readi(
                        handle_,
                        samples.data(),
                        static_cast<snd_pcm_uframes_t>(chunk_frames)    // frame数量，不是sample数，也不是字节数
                    );

                if (read_result == -EINTR) {
                    continue;
                }

                if (read_result < 0) {
                    if (stopped.load() || capture_cancelled_.load()) {
                        // 丢弃尚未读取的数据，停止采集
                        // drop后必须再调用一次prepare才能通过readi再次调用
                        snd_pcm_drop(handle_);
                        
                        // 外接终止，正常结束，不报错
                        return PcmStreamResult::success(total_frames);
                    }

                    const int recovered = snd_pcm_recover(
                        handle_,
                        static_cast<int>(read_result),
                        1
                    );

                    if (recovered < 0) {
                        snd_pcm_drop(handle_);

                        return PcmStreamResult::failure(
                            alsaError(
                                "failed to recover ALSA PCM stream",
                                recovered
                            )
                        );
                    }

                    continue;
                }

                if (read_result == 0) {
                    continue;
                }

                const std::size_t actual_frames = 
                    static_cast<std::size_t>(read_result);

                AudioBuffer chunk;
                chunk.sample_rate = config_.sample_rate;
                chunk.channels = config_.channels;

                // 复制实际读取到的数据
                chunk.samples.assign(
                    samples.begin(),
                    samples.begin()
                        + static_cast<std::ptrdiff_t>(actual_frames * channels)
                );

                total_frames += actual_frames;

                // 回调函数为0表示结束麦克风读取
                if (!handler(chunk)) {
                    // 丢弃尚未读取的数据
                    snd_pcm_drop(handle_);

                    return PcmStreamResult::success(total_frames);
                }
            }

            snd_pcm_drop(handle_);

            return PcmStreamResult::success(total_frames);
        } catch (const std::exception& error) {
            snd_pcm_drop(handle_);

            return PcmStreamResult::failure(
                "PCM chunk handler failed: "
                    + std::string(error.what())
            );
        } catch (...) {
            snd_pcm_drop(handle_);

            return PcmStreamResult::failure("PCM chunk handler failed with unknown error");
        }
    }

    void cancelCurrentCapture() {
        capture_cancelled_.store(true);

        /*
        * 解除可能阻塞的 snd_pcm_readi()。
        * 下一次 capture() 会重新调用 prepare。
        */
        if (handle_ != nullptr) {
            snd_pcm_drop(handle_);
        }
    }

    void stop() {
        stopped.store(true);
        cancelCurrentCapture();
    }

private:
    AlsaPcmStreamSourceConfig config_;

    snd_pcm_t *handle_ = nullptr;

    std::atomic_bool capture_cancelled_{false};
    std::atomic_bool stopped{false};
    std::mutex capture_mutex_;
};

AlsaPcmStreamSource::AlsaPcmStreamSource(AlsaPcmStreamSourceConfig config)
    : config_(config) {
    validateConfig(config_);

    impl_ = std::make_unique<Impl>(config_);
}

AlsaPcmStreamSource::~AlsaPcmStreamSource() = default;

std::string AlsaPcmStreamSource::name() const {
    return "alsa_pcm_stream";
}

int AlsaPcmStreamSource::sampleRate() const {
    return config_.sample_rate;
}

int AlsaPcmStreamSource::channels() const {
    return config_.channels;
}

PcmStreamResult AlsaPcmStreamSource::capture(
    const PcmChunkHandler& handler
) {
    if (!impl_) {
        return PcmStreamResult::failure(
            "ALSA PCM stream source is not initialized"
        );
    }

    return impl_->capture(handler);
}

void AlsaPcmStreamSource::cancelCurrentCapture() {
    if (impl_) {
        impl_->cancelCurrentCapture();
    }
}

void AlsaPcmStreamSource::stop() {
    if (impl_) {
        impl_->stop();
    }
}

const AlsaPcmStreamSourceConfig& AlsaPcmStreamSource::config() const {
    return config_;
}
