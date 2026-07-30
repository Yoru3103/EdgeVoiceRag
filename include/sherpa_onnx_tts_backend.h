#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tts_backend.h"

struct SherpaOnnxTtsConfig {
    std::string model_path;
    std::string lexicon_path;
    std::string tokens_path;

    /*
     * 文本规范化规则：
     * date.fst：日期
     * number.fst：数字
     * phone.fst：电话号码，可按需要加入
     */
    std::vector<std::string> rule_fsts;

    int num_threads = 2;
    int speaker_id = 0;
    int max_num_sentences = 2;

    float speed = 1.0F;
    float silence_scale = 0.2F;

    float noise_scale = 0.667F;
    float noise_scale_w = 0.8F;
    float length_scale = 1.0F;

    std::string provider = "cpu";
    bool debug = false;
};

class SherpaOnnxTtsBackend final : public TtsBackend {
public:
    explicit SherpaOnnxTtsBackend(SherpaOnnxTtsConfig config);

    ~SherpaOnnxTtsBackend() override;

    SherpaOnnxTtsBackend(const SherpaOnnxTtsBackend&) = delete;
    SherpaOnnxTtsBackend& operator=(const SherpaOnnxTtsBackend&) = delete;

    std::string name() const override;

    TtsSynthesisResult synthesize(const std::string& text) override;

    const SherpaOnnxTtsConfig& config() const;

private:
    // 同ASR，避免通过头文件传播给别的模块
    class Impl;

    SherpaOnnxTtsConfig config_;
    std::unique_ptr<Impl> impl_;
};
