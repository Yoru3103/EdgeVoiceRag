#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "audio_recorder.h"
#include "pcm_stream_source.h"

struct SherpaOnnxVadConfig {
    std::string model_path;

    int sample_rate = 16000;
    int num_threads = 1;
    int window_size = 512;                  // 每次送给 Silero 模型分析的采样点数

    float threshold = 0.25F;                // 语音概率高于该值认为存在语音

    float min_silence_duration = 0.8F;      // 连续静音多久认为用户说完
    float min_speech_duration = 0.25F;      // 过滤敲击、咳嗽等短声音
    float max_speech_duration = 15.0F;      // 避免录音无限持续
    float max_wait_seconds = 10.0F;         // 等待用户开口的最长时间

    float buffer_size_seconds = 30.0F;      // VAD内部环形缓冲区容量

    std::string provider = "cpu";
    bool debug = false;
};

// 接收ALSA的chunk，使用Silero VAD判断语音开始和结束，返回AudioBuffer
class SherpaOnnxVadAudioRecorder final : public AudioRecorder {
public:
    SherpaOnnxVadAudioRecorder(
        PcmStreamSource& source,
        SherpaOnnxVadConfig config
    );

    ~SherpaOnnxVadAudioRecorder() override;

    SherpaOnnxVadAudioRecorder(const SherpaOnnxVadAudioRecorder&) = delete;
    SherpaOnnxVadAudioRecorder& operator=(const SherpaOnnxVadAudioRecorder&) = delete;

    std::string name() const override;

    AudioCaptureResult recordUtterance() override;

    void stop() override;

    const SherpaOnnxVadConfig& config() const;

private:
    class Impl;

    PcmStreamSource& source_;           // 音频采集模块
    SherpaOnnxVadConfig config_;        
    std::unique_ptr<Impl> impl_;

    std::atomic_bool stopped_{false};
};
