#include "multi_level_response_system.h"

#include <utility>

MultiLevelResponseSystem::MultiLevelResponseSystem(
    ResponseBackend& backend,
    std::size_t cache_limit
) : backend_(backend), cache_limit_(cache_limit) {
}

ResponseDecision MultiLevelResponseSystem::decide(const std::string& query) const {
    ResponseDecision decision;

    decision.classification = classifier_.classify(query);

    switch (decision.classification.category) {
        case QueryCategory::Emergency:
        case QueryCategory::Factual:
            decision.mode = ResponseMode::RagOnly;
            break;

        case QueryCategory::Creative:
            decision.mode = ResponseMode::LlmOnly;
            break;

        case QueryCategory::Complex:
        case QueryCategory::Unknown:
        default:
            decision.mode = ResponseMode::Hybrid;
    }

    return decision;
}

QueryProcessResult MultiLevelResponseSystem::process(const std::string& query) {
    if (query.empty()) {
        QueryProcessResult result;
        result.query = query;
        result.error = "query is empty";
        return result;
    }

    const auto cached = cache_.find(query);

    if (cached != cache_.end()) {
        QueryProcessResult result = cached->second;

        result.from_cache = true;

        return result;
    }

    const ResponseDecision decision = decide(query);

    QueryProcessResult result;

    switch (decision.mode) {
        case ResponseMode::RagOnly:
            result = processRagOnly(query, decision);
            break;

        case ResponseMode::LlmOnly:
            result = processLlmOnly(query, decision);
            break;

        case ResponseMode::Hybrid:
        default:
            result = processHybrid(query, decision);
            break;
    }

    if (result.ok) {
        addToCache(query, result);
    }

    return result;
}

QueryProcessResult MultiLevelResponseSystem::processRagOnly(
    const std::string& query,
    const ResponseDecision& decision
) {
    const BackendResult rag_result = backend_.searchRag(query);

    if (!rag_result.ok) {
        return makeFailure(query, decision, "RAG failed: " + rag_result.error);
    }

    if (rag_result.text.empty()) {
        return makeFailure(query, decision, "RAG returned empty answer");
    }

    QueryProcessResult result;

    result.ok = true;
    result.query = query;
    result.answer = rag_result.text;
    result.category = decision.classification.category;
    result.mode = decision.mode;

    return result;
}

QueryProcessResult
MultiLevelResponseSystem::processLlmOnly(
    const std::string& query,
    const ResponseDecision& decision
) {
    const BackendResult llm_result = backend_.generateLlm(query);

    if (!llm_result.ok) {
        return makeFailure(query, decision, "LLM failed: " + llm_result.error);
    }

    if (llm_result.text.empty()) {
        return makeFailure(query, decision, "LLM returned empty answer");
    }

    QueryProcessResult result;

    result.ok = true;
    result.query = query;
    result.answer = llm_result.text;
    result.category = decision.classification.category;
    result.mode = decision.mode;

    return result;
}

QueryProcessResult MultiLevelResponseSystem::processHybrid(
    const std::string& query,
    const ResponseDecision& decision
) {
    const BackendResult rag_result = backend_.searchRag(query);

    std::string prompt = query;

    if (rag_result.ok && !rag_result.text.empty()) {
        prompt += "\n<rag>\n";
        prompt += rag_result.text;
        prompt += "\n</rag>";
    }

    const BackendResult llm_result = backend_.generateLlm(prompt);

    if (!llm_result.ok) {
        return makeFailure(query, decision, "hybrid LLM failed " + llm_result.error);
    }

    if (llm_result.text.empty()) {
        return makeFailure(query, decision, "hybrid LLM returned empty answer");
    }

    QueryProcessResult result;
    result.ok = true;
    result.query = query;
    result.answer = llm_result.text;
    result.category = decision.classification.category;
    result.mode = decision.mode;

    return result;
}

QueryProcessResult MultiLevelResponseSystem::makeFailure(
    const std::string& query,
    const ResponseDecision& decision,
    const std::string& error
) const {
    QueryProcessResult result;

    result.ok = false;
    result.query = query;
    result.error = error;
    result.category = decision.classification.category;
    result.mode = decision.mode;

    return result;
}

void MultiLevelResponseSystem::addToCache(
    const std::string& query,
    const QueryProcessResult& result
) {
    if (cache_limit_ == 0) {
        return;
    }

    if (cache_.size() >= cache_limit_) {
        cache_.clear();
    }

    cache_[query] = result;
}

void MultiLevelResponseSystem::clearCache() {
    cache_.clear();
}

std::size_t MultiLevelResponseSystem::cacheSize() const {
    return cache_.size();
}

std::string MultiLevelResponseSystem::modeToString(ResponseMode mode) const {
    switch (mode) {
        case ResponseMode::RagOnly:
            return "rag_only";

        case ResponseMode::LlmOnly:
            return "llm_only";

        case ResponseMode::Hybrid:
        default:
            return "hybrid";
    }
}