#pragma once

#include <cstddef>
#include <string>

#include "llm_backend.h"

// 不依赖模型的 C++ 推理占位实现
class MockLlmBackend : public LlmBackend {
public:
    explicit MockLlmBackend(std::size_t preview_length = 60);

    std::string name() const override;
    LlmGenerationResult generate(const std::string& prompt) override;

private:
    std::size_t preview_length_;
};