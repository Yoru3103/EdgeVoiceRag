#include "response_policy.h"

#include <cmath>
#include <stdexcept>

ResponsePolicy::ResponsePolicy(ResponsePolicyConfig config)
    : config_(config) {
    if (
        !std::isfinite(config_.direct_rag_minimum_sparse_score)
        || config_.direct_rag_minimum_sparse_score <= 0.0F
    ) {
        throw std::invalid_argument(
            "direct RAG sparse threshold must be finite and greater than zero"
        );
    }

    if (
        !std::isfinite(config_.direct_rag_minimum_dense_similarity)
        || config_.direct_rag_minimum_dense_similarity <= 0.0F
        || config_.direct_rag_minimum_dense_similarity >= 1.0F
    ) {
        throw std::invalid_argument(
            "direct RAG dense threshold must be finite and between zero and one"
        );
    }
}

ResponsePolicyDecision ResponsePolicy::decideWithoutRetrieval(
    const QueryClassification& classification
) const {
    ResponsePolicyDecision decision;
    decision.classification = classification;

    if (classification.category == QueryCategory::Creative) {
        decision.mode = PolicyResponseMode::LlmOnly;
        decision.reason = "creative query does not require vehicle retrieval";
        return decision;
    }

    decision.mode = PolicyResponseMode::RagLlm;
    decision.reason = "vehicle retrieval is required before routing";
    return decision;
}

ResponsePolicyDecision ResponsePolicy::decideWithRetrieval(
    const QueryClassification& classification,
    const std::vector<RetrievalResult>& results
) const {
    ResponsePolicyDecision decision;
    decision.classification = classification;
    decision.retrieval_performed = true;
    decision.retrieval_result_count = results.size();
    decision.high_confidence_retrieval =
        !results.empty() && isHighConfidence(results.front());

    if (classification.category == QueryCategory::Emergency) {
        decision.mode = PolicyResponseMode::Safety;
        decision.reason = results.empty()
            ? "safety-critical query without reliable manual context"
            : "safety-critical query uses guarded response with manual context";
        return decision;
    }

    if (
        classification.category == QueryCategory::Factual
        && decision.high_confidence_retrieval
    ) {
        decision.mode = PolicyResponseMode::DirectRag;
        decision.reason = "factual query has high-confidence manual evidence";
        return decision;
    }

    if (!results.empty()) {
        decision.mode = PolicyResponseMode::RagLlm;
        decision.reason = decision.high_confidence_retrieval
            ? "manual evidence requires generated synthesis"
            : "manual evidence is relevant but below direct-answer threshold";
        return decision;
    }

    if (
        classification.category == QueryCategory::Complex
        || classification.category == QueryCategory::Creative
    ) {
        decision.mode = PolicyResponseMode::LlmOnly;
        decision.reason = "reasoning query has no relevant manual evidence";
        return decision;
    }

    decision.mode = PolicyResponseMode::Clarification;
    decision.reason = "query is ambiguous and no relevant manual evidence was found";
    return decision;
}

bool ResponsePolicy::isHighConfidence(
    const RetrievalResult& result
) const noexcept {
    return
        result.sparse_score >= config_.direct_rag_minimum_sparse_score
        || result.dense_score >= config_.direct_rag_minimum_dense_similarity;
}

std::string ResponsePolicy::modeToString(PolicyResponseMode mode) {
    switch (mode) {
        case PolicyResponseMode::Safety:
            return "safety";
        case PolicyResponseMode::DirectRag:
            return "direct_rag";
        case PolicyResponseMode::RagLlm:
            return "rag_llm";
        case PolicyResponseMode::LlmOnly:
            return "llm_only";
        case PolicyResponseMode::Clarification:
        default:
            return "clarification";
    }
}
