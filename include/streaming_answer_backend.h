#pragma once

#include <functional>
#include <string>

#include "rag_stream_protocol.h"

// 后端流式输出事件
using RagStreamEventHandler = std::function<void(const RagStreamEvent&)>;

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

class StreamingAnswerBackend {
public:
    virtual ~StreamingAnswerBackend() = default;

    virtual std::string name() const = 0;

    virtual RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const = 0;
};
