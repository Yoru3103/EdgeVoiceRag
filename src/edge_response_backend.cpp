#include "edge_response_backend.h"

#include <sstream>
#include <utility>
#include <vector>

#include "rag_response_parser.h"

EdgeResponseBackend::EdgeResponseBackend(
    RagEngine& rag_engine,
    TextRequester& requester,
    std::string rag_backend,
    std::string rag_endpoint,
    int rag_timeout_ms,
    std::string llm_endpoint,
    int llm_timeout_ms,
    int top_k
)
    : rag_engine_(rag_engine),
      requester_(requester),
      rag_backend_(std::move(rag_backend)),
      rag_endpoint_(std::move(rag_endpoint)),
      rag_timeout_ms_(rag_timeout_ms),
      llm_endpoint_(std::move(llm_endpoint)),
      llm_timeout_ms_(llm_timeout_ms),
      top_k_(top_k) {
}

BackendResult EdgeResponseBackend::searchRag(const std::string& query) {
    if (rag_backend_ == "local") {
        return searchLocalRag(query);
    }

    if (rag_backend_ == "zmq" || rag_backend_ == "python_zmq") {
        return searchZmqRag(query);
    }

    return BackendResult::failure(
        "unsupported RAG backend: "
        + rag_backend_
    );
}

BackendResult EdgeResponseBackend::generateLlm(const std::string& prompt) {
    const BackendResult response = requester_.request(
        llm_endpoint_,
        prompt,
        llm_timeout_ms_
    );

    if (!response.ok) {
        return response;
    }

    const std::string answer = RagResponseParser::extractAnswerOrRaw(response.text);

    if (answer.empty()) {
        return BackendResult::failure(
            "LLM returned empty response"
        );
    }

    if (answer.rfind("[ERROR]", 0) == 0) {
        return BackendResult::failure(answer);
    }

    return BackendResult::success(answer);
}

BackendResult EdgeResponseBackend::searchLocalRag(const std::string& query) {
    const std::vector<SearchResult> results = rag_engine_.searchTopK(query, top_k_);

    if (results.empty()) {
        return BackendResult::failure(
            "no local RAG result"
        );
    }

    return BackendResult::success(
        buildLocalAnswer(results)
    );
}

BackendResult EdgeResponseBackend::searchZmqRag(const std::string& query) {
    const BackendResult response = requester_.request(
        rag_endpoint_,
        query,
        rag_timeout_ms_
    );

    if (!response.ok) {
        return response;
    }

    const std::string answer = RagResponseParser::extractRagAnswerOrRaw(response.text);

    if (answer.empty()) {
        return BackendResult::failure(
            "RAG returned empty response"
        );
    }

    if (
        answer.rfind("[ERROR]", 0)
        == 0
    ) {
        return BackendResult::failure(answer);
    }

    return BackendResult::success(answer);
}

std::string EdgeResponseBackend::buildLocalAnswer(const std::vector<SearchResult>& results) {
    std::ostringstream answer;

    answer << "根据车辆手册：\n";

    for (std::size_t i = 0; i < results.size(); i++) {
        answer
            << i + 1
            << ". "
            << results[i].document
            << " [score="
            << results[i].score
            << "]";

        if (i + 1 < results.size()) {
            answer << '\n';
        }
    }
    return answer.str();
}