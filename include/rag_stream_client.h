#pragma once

#include <functional>
#include <string>

#include "rag_stream_protocol.h"

using RagStreamEventHandler = std::function<void(const RagStreamEvent&)>;

// 客户端返回结果
struct RagStreamQueryResult {
    bool ok = false;

    std::string request_id;
    std::string answer;

    std::string backend;
    std::string llm_backend;

    std::string error;

    double elapsed_ms = 0.0;

    static RagStreamQueryResult success(
        const std::string& request_id,
        const std::string& answer,
        const std::string& backend,
        const std::string& llm_backend,
        double elapsed_ms
    );

    static RagStreamQueryResult failure(
        const std::string& request_id,
        const std::string& error
    );
};

class RagStreamClient {
public:
    explicit RagStreamClient(
        std::string endpoint,
        int timeout_ms = 120000
    );

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const;

    const std::string& endpoint() const;

    int timeoutMilliseconds() const;

private:
    std::string endpoint_;
    int timeout_ms_;
};
