#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "rag_stream_protocol.h"

// 后端流式输出事件
using RagStreamEventHandler = std::function<void(const RagStreamEvent&)>;

struct RagQueryTiming {
    // Retriever::searchTopK() 耗时。
    double retrieval_elapsed_ms = 0.0;

    // 从调用 LLM generateStream() 到收到第一个非空 Token/Chunk。
    double llm_time_to_first_token_ms = 0.0;

    // 完整 generateStream() 调用耗时。
    double llm_elapsed_ms = 0.0;

    // 是否收到过流式chunk
    bool first_token_observed = false;
};
struct RagStreamQueryResult {
    bool ok = false;

    std::string request_id;
    std::string answer;

    std::string backend;
    std::string llm_backend;

    std::string error;

    double elapsed_ms = 0.0;
    RagQueryTiming timing;

    // 可选的响应策略元数据。普通后端保持默认空值。
    std::string response_mode;
    std::string response_reason;
    std::string query_category;
    float classification_confidence = 0.0F;
    std::size_t retrieval_result_count = 0;

    static RagStreamQueryResult success(
        const std::string& request_id,
        const std::string& answer,
        const std::string& backend,
        const std::string& llm_backend,
        double elapsed_ms,
        RagQueryTiming timing = {}
    );

    static RagStreamQueryResult failure(
        const std::string& request_id,
        const std::string& error,
        double elapsed_ms = 0.0,
        RagQueryTiming timing = {}
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
