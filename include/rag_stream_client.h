#pragma once

#include <string>

#include "streaming_answer_backend.h"

// final表示不希望其他类继续继承该类
class RagStreamClient final
    : public StreamingAnswerBackend {
public:
    explicit RagStreamClient(
        std::string endpoint,
        int timeout_ms = 120000
    );

    std::string name() const override;

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const override;

    const std::string& endpoint() const;

    int timeoutMilliseconds() const;

private:
    std::string endpoint_;
    int timeout_ms_;
};
