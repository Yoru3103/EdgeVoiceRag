#pragma once

#include <functional>
#include <mutex>
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

    bool cancel(const std::string& request_id);

    std::string activeRequestId() const;

private:
    LlmBackend& backend_;

    mutable std::mutex active_mutex_;
    std::string active_request_id_;

    void setActiveRequest(const std::string& request_id);
    void clearActiveRequest(const std::string& request_id);

    static std::string tryExtractRequestId(const std::string& message);
};
