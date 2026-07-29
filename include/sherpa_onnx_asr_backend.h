#pragma once

#include <memory>
#include <string>

#include "asr_backend.h"

struct SherpaOnnxAsrConfig {
    std::string model_path;
    std::string tokens_path;

    std::string language = "zh";
    bool use_itn = true;

    int num_threads = 2;

    std::string provider = "cpu";
    bool debug = false;
};

class SherpaOnnxAsrBackend final : public AsrBackend {
public:
    explicit SherpaOnnxAsrBackend(SherpaOnnxAsrConfig config);

    ~SherpaOnnxAsrBackend();

    SherpaOnnxAsrBackend(const SherpaOnnxAsrBackend&) = delete;
    SherpaOnnxAsrBackend& operator=(const SherpaOnnxAsrBackend&) = delete;

    std::string name() const override;

    AsrTranscriptionResult transcribe(const AudioBuffer& audio) override;

    const SherpaOnnxAsrConfig& config() const;

private:
    class Impl;

    SherpaOnnxAsrConfig config_;
    std::unique_ptr<Impl> impl_;    // 普通代码不直接依赖sherpa头文件，API不会传播到所有文件，编译速度更好
};
