#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "query_classifier.h"
#include "retrieval_types.h"

enum class PolicyResponseMode {
    Safety,             // Emergency
    DirectRag,          // Factual且检索结果高置信
    RagLlm,             // 有相关检索结果但不满足直答条件
    LlmOnly,            // Complex且没有检索结果
    Clarification       // Unknown且没有检索结果
};

struct ResponsePolicyConfig {
    float direct_rag_minimum_sparse_score = 8.0F;
    float direct_rag_minimum_dense_similarity = 0.55F;
};

struct ResponsePolicyDecision {
    PolicyResponseMode mode = PolicyResponseMode::Clarification;
    QueryClassification classification;

    bool retrieval_performed = false;
    std::size_t retrieval_result_count = 0;
    bool high_confidence_retrieval = false;

    std::string reason;
};

class ResponsePolicy final {
public:
    explicit ResponsePolicy(ResponsePolicyConfig config = {});

    ResponsePolicyDecision decideWithoutRetrieval(
        const QueryClassification& classification
    ) const;

    ResponsePolicyDecision decideWithRetrieval(
        const QueryClassification& classification,
        const std::vector<RetrievalResult>& results
    ) const;

    bool isHighConfidence(
        const RetrievalResult& result
    ) const noexcept;

    static std::string modeToString(PolicyResponseMode mode);

private:
    ResponsePolicyConfig config_;
};
