#include "alsa_audio_player.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

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
    const AlsaAudioPlayerConfig& config
) {
    if (config.device.empty()) {
        throw std::invalid_argument(
            "ALSA playback device must not be empty"
        );
    }

    if (config.period_frames == 0) {
        throw std::invalid_argument(
            "ALSA playback period_frames must "
            "be greater than zero"
        );
    }
}

void validateAudio(const AudioBuffer& audio) {
    if (audio.empty()) {
        throw std::invalid_argument(
            "playback audio must not be empty"
        );
    }

    if (audio.sample_rate <= 0) {
        throw std::invalid_argument(
            "playback sample rate must be "
            "greater than zero"
        );
    }

    if (audio.channels <= 0) {
        throw std::invalid_argument(
            "playback channels must be "
            "greater than zero"
        );
    }

    const auto channels =
        static_cast<std::size_t>(audio.channels);

    if (
        audio.samples.size() % channels != 0
    ) {
        throw std::invalid_argument(
            "playback samples are not aligned "
            "to channel count"
        );
    }
}

}   // namespace

class AlsaAudioPlayer::Impl {
public:
    explicit Impl(const AlsaAudioPlayerConfig& config)
        : config_(config) {
        // 打开设备
        const int result = snd_pcm_open(
            &handle_,   // 文件句柄，指向打开的PCM设备对象
            config_.device.c_str(),     // 设备名称
            SND_PCM_STREAM_PLAYBACK,    // 表示打开播放设备
            0                           // 默认阻塞模式
        );

        if (result < 0) {
            handle_ = nullptr;

            throw std::runtime_error(
                alsaError(
                    "failed to open ALSA playback device "
                        + config_.device,
                    result
                )
            );
        }
    }
    
    ~Impl() {
        stop();

        // 等待正在执行的play退出后再关闭handle
        std::lock_guard<std::mutex> lock(play_mutex_);

        if (handle_ != nullptr) {
            snd_pcm_close(handle_);
            handle_ = nullptr;
        }
    }

    AudioPlaybackResult play(const AudioBuffer& audio) {
        try {
            validateAudio(audio);

            // 防止两个线程同时播放
            std::lock_guard<std::mutex> lock(play_mutex_);

            if (handle_ == nullptr) {
                return AudioPlaybackResult::failure(
                    "ALSA playback device is not initialized"
                );
            }

            // 清除停止标志，开始一次新的播放
            stop_requested_.store(false);

            // 每次播放都要重新配置一次，这样可以允许audiobuffer每次使用不同的采样率或声道数
            // 但会带来一定配置开销
            configureDevice(audio);

            // 计算帧数
            const std::size_t channels = static_cast<std::size_t>(audio.channels);
            const std::size_t total_frames = audio.frameCount();

            std::size_t written_frames = 0;
            // 分块写入音频
            while (written_frames < total_frames) {
                if (stop_requested_.load()) {
                    // 用户打断，不属于设备故障
                    return AudioPlaybackResult::success();
                }

                const std::size_t remaining_frames = total_frames - written_frames;
                
                // 决定本次写入帧数，当剩余帧数比config块帧数小时，配置为剩余帧数
                const snd_pcm_uframes_t request_frames = 
                    static_cast<snd_pcm_uframes_t>(
                        std::min<std::size_t>(
                            remaining_frames,
                            config_.period_frames
                        )
                    );
                
                // 计算本次数据起始地址 
                const std::int16_t* data = audio.samples.data() + written_frames * channels;
                const snd_pcm_sframes_t result = 
                    snd_pcm_writei(
                        handle_,
                        data,           // PCM数据首地址
                        request_frames  // 希望写入的帧数
                    );

                // 被信号中断，非声卡故障
                if (result == -EINTR) {
                    continue;
                }

                if (result < 0) {
                    if (stop_requested_.load()) {
                        return AudioPlaybackResult::success();
                    }

                    // 非打断时，尝试从常见PCM错误中恢复
                    const int recovered = 
                        snd_pcm_recover(
                            handle_,
                            static_cast<int>(result),
                            1
                        );

                    if (recovered < 0) {
                        return AudioPlaybackResult::failure(
                            alsaError(
                                "failed to recover ALSA playback",
                                recovered
                            )
                        );
                    }

                    continue;
                }

                // 表示这一次没有写入帧，代码直接重试
                if (result == 0) {
                    continue;
                }

                // 确定成功后再更新已写帧数
                written_frames += static_cast<std::size_t>(result);
            }

            if (stop_requested_.load()) {
                return AudioPlaybackResult::success();
            }

            // 等待ALSA缓冲区中剩余数据播放结束
            const int drain_result = snd_pcm_drain(handle_);

            if (
                drain_result < 0 &&
                !stop_requested_.load()
            ) {
                return AudioPlaybackResult::failure(
                    alsaError(
                        "failed to drain ALSA playback",
                        drain_result
                    )
                );
            }

            return AudioPlaybackResult::success();
        } catch (const std::exception& error) {
            return AudioPlaybackResult::failure(
                "ALSA playback failed: "
                    + std::string(error.what())
            );
        }
    }

    void stop() {
        stop_requested_.store(true);

        /*
         * snd_pcm_drop() 会立即丢弃尚未播放的数据，
         * 也会使阻塞中的 snd_pcm_writei() 尽快返回。
         *
         * handle_ 从构造到析构始终存在，因此这里不需要
         * 临时关闭设备。
         */
        if (handle_ != nullptr) {
            snd_pcm_drop(handle_);
        }
    }

private:
    void configureDevice(const AudioBuffer& audio) {
        snd_pcm_hw_params_t* params = nullptr;
        // 在当前函数的栈上分配一个硬件参数对象，函数退出后自动释放
        snd_pcm_hw_params_alloca(&params);  // 传入指针的指针，函数是修改指针本身让其指向新的内存

        // 初始化参数对象
        requireAlsaSuccess(
            snd_pcm_hw_params_any(
                handle_,
                params
            ),
            "failed to initialize playback parameters"
        );

        // 设置交错存储（之前读取buffer的声道交错排列）
        requireAlsaSuccess(
            snd_pcm_hw_params_set_access(
                handle_,
                params,
                SND_PCM_ACCESS_RW_INTERLEAVED
            ),
            "failed to set interleaved playback"
        );

        // 设置PCM格式（16位有符号整数，小端存储）
        requireAlsaSuccess(
            snd_pcm_hw_params_set_format(
                handle_,
                params,
                SND_PCM_FORMAT_S16_LE
            ),
            "failed to set S16_LE playback format"
        );

        // 声道数
        requireAlsaSuccess(
            snd_pcm_hw_params_set_channels(
                handle_,
                params,
                static_cast<unsigned int>(audio.channels)
            ),
            "failed to set playback channels"
        );

        /*
         * 当前不在播放器中做重采样。
         * 声卡必须接受 AudioBuffer 的原始采样率。
         *
         * default/plughw 通常能够通过 ALSA 插件完成转换。
         */
        requireAlsaSuccess(
            snd_pcm_hw_params_set_rate(
                handle_,
                params,
                static_cast<unsigned int>(audio.sample_rate),
                0   // 不要向上或向下寻找邻近采样率
            ),
            "failed to set playback sample rate"
        );

        // 设置period大小（声卡每次处理的一块音频帧）
        snd_pcm_uframes_t period_frames = static_cast<snd_pcm_uframes_t>(config_.period_frames);

        requireAlsaSuccess(
            snd_pcm_hw_params_set_period_size_near(
                handle_,
                params,
                &period_frames,
                nullptr
            ),
            "failed to set playback period size"
        );

        // 将配置提交给PCM设备
        requireAlsaSuccess(
            snd_pcm_hw_params(
                handle_,
                params
            ),
            "failed to apply playback parameters"
        );

        requireAlsaSuccess(
            snd_pcm_prepare(handle_),
            "failed to prepare playback device"
        );
    }

    AlsaAudioPlayerConfig config_;
    snd_pcm_t* handle_ = nullptr;   // ALSA PCM 播放设备的句柄，表示当前打开的声卡播放通道。
    std::atomic_bool stop_requested_{false};

    // 防止两个线程同时调用play
    std::mutex play_mutex_;
};

AlsaAudioPlayer::AlsaAudioPlayer(AlsaAudioPlayerConfig config)
    : config_(std::move(config)) {
    validateConfig(config_);

    impl_ = std::make_unique<Impl>(config_);
}

AlsaAudioPlayer::~AlsaAudioPlayer() = default;

std::string AlsaAudioPlayer::name() const {
    return "alsa_playback";
}

AudioPlaybackResult AlsaAudioPlayer::play(const AudioBuffer& audio) {
    if (!impl_) {
        return AudioPlaybackResult::failure(
            "ALSA player is not initialized"
        );
    }

    return impl_->play(audio);
}

void AlsaAudioPlayer::stop() {
    if (impl_) {
        impl_->stop();
    }
}

const AlsaAudioPlayerConfig& AlsaAudioPlayer::config() const {
    return config_;
}
