#pragma once

#include <string>
#include <vector>

#include "retriever.h"

struct RelevanceFilteringRetrieverConfig {
    // 当前阈值来自 benchmarks/retrieval_cases.json。
    float minimum_sparse_score = 6.0F;
    float minimum_dense_similarity = 0.40F;

    int candidate_top_k = 20;
};

class RelevanceFilteringRetriever final : public Retriever {
public:
    RelevanceFilteringRetriever(
        Retriever& inner_retriever,
        RelevanceFilteringRetrieverConfig config
    );

    std::vector<RetrievalResult> searchTopK(
        const std::string& query,
        int top_k
    ) const override;

private:
    bool isRelevant(const RetrievalResult& result) const noexcept;

    void validateConfig() const;

    Retriever& inner_retriever_;
    RelevanceFilteringRetrieverConfig config_;
};
