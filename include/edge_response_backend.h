#pragma once

#include <string>

#include "retriever.h"
#include "response_backend.h"
#include "zmq_text_client.h"

// 负责三种调用，还负责解析服务器返回的json
class EdgeResponseBackend : public ResponseBackend {
public:
    EdgeResponseBackend(
        Retriever& retriever,
        TextRequester& requester,
        std::string rag_backend,
        std::string rag_endpoint,
        int rag_timeout_ms,
        std::string llm_endpoint,
        int llm_timeout_ms,
        int top_k
    );

    BackendResult searchRag(const std::string& query) override;
    BackendResult generateLlm(const std::string& prompt) override;

private:
    Retriever& retriever_;
    TextRequester& requester_;

    std::string rag_backend_;
    std::string rag_endpoint_;
    int rag_timeout_ms_;

    std::string llm_endpoint_;
    int llm_timeout_ms_;

    int top_k_;

    BackendResult searchLocalRag(const std::string& query);
    BackendResult searchZmqRag(const std::string& query);

    static std::string buildLocalAnswer(
        const std::vector<RetrievalResult>& results
    );
};