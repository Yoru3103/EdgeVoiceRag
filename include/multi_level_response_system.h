#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "query_classifier.h"
#include "response_backend.h"

enum class ResponseMode {
    RagOnly,
    LlmOnly,
    Hybrid
};

struct ResponseDecision {
    QueryClassification classification;
    ResponseMode mode = ResponseMode::Hybrid;
};

struct QueryProcessResult {
    bool ok = false;

    std::string query;
    std::string answer;
    std::string error;

    QueryCategory category = (
        QueryCategory::Unknown
    );

    ResponseMode mode = ResponseMode::Hybrid;

    bool from_cache = false;
};

class MultiLevelResponseSystem {
public:
    explicit MultiLevelResponseSystem(
        ResponseBackend& backend,
        std::size_t cache_limit = 100
    );

    ResponseDecision decide(const std::string& query) const;

    QueryProcessResult process(const std::string& query);

    void clearCache();

    std::size_t cacheSize() const;

    std::string modeToString(ResponseMode mode) const;

private:
    QueryClassifier classifier_;
    ResponseBackend& backend_;

    std::size_t cache_limit_;

    std::unordered_map<std::string, QueryProcessResult> cache_;

    QueryProcessResult processRagOnly(
        const std::string& query,
        const ResponseDecision& decision
    );

    QueryProcessResult processLlmOnly(
        const std::string& query,
        const ResponseDecision& decision
    );

    QueryProcessResult processHybrid(
        const std::string& query,
        const ResponseDecision& decision
    );

    QueryProcessResult makeFailure(
        const std::string& query,
        const ResponseDecision& decision,
        const std::string& error
    ) const;

    void addToCache(
        const std::string& query,
        const QueryProcessResult& result
    );
};