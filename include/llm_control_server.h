#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "llm_stream_service.h"

class LlmControlServer {
public:
    LlmControlServer(
        LlmStreamService& stream_service,
        std::string endpoint
    );

    ~LlmControlServer();

    LlmControlServer(const LlmControlServer&) = delete;
    LlmControlServer& operator=(const LlmControlServer&) = delete;

    void start();
    void stop();

    bool running() const;

private:
    LlmStreamService& stream_service_;
    std::string endpoint_;

    std::atomic_bool running_{false};
    std::thread worker_;

    void run();
    std::string handleMessage(const std::string& message);
};
