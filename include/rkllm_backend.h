#pragma once

#include <string>

#include "llm_backend.h"

struct RkllmBackendConfig {
    std::string model_path;
    int max_new_tokens = 512;
    int max_context_len = 4096;
    int top_k = 1;
    float top_p = 0.95F;
    float temperature = 0.8F;
    float repeat_penalty = 1.1F;
};

class RkllmBackend : public LlmBackend {
public:
    explicit RkllmBackend(const RkllmBackendConfig& config);
    ~RkllmBackend() override;

    RkllmBackend(const RkllmBackend&) = delete;
    RkllmBackend& operator=(const RkllmBackend&) = delete;

    std::string name() const override;
    LlmGenerationResult generate(const std::string& prompt) override;

    LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) override;

    bool cancel() override;

private:
    // 使用Impl隐藏RKLLM SDK类型。这样PC编译时，其他模块不需要包含rkllm.h，也不会把RKLLM SDK依赖扩散到整个项目
    struct Impl;
    Impl* impl_;

    LlmGenerationResult run(
        const std::string& prompt,
        const LlmChunkCallback* callback
    );
};
