#pragma once

#include <string>

#include "llm_backend.h"

// 协议校验、调用后端、耗时统计、响应生成
class LlmService {
public:
    explicit LlmService(LlmBackend& backend);

    std::string handleMessage(const std::string& message);

private:
    LlmBackend& backend_;

    static std::string tryExtractRequestId(const std::string& message);
};