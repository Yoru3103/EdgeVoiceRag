#pragma once

#include <functional>
#include <string>

#include "llm_backend.h"

using LlmEventEmitter = std::function<void(const std::string&)>;

class LlmStreamService {
public:
    explicit LlmStreamService(LlmBackend& backend);

    void handleMessage(
        const std::string& message,
        const LlmEventEmitter& emitter
    );

private:
    LlmBackend& backend_;

    static std::string tryExtractRequestId(const std::string& message);
};
