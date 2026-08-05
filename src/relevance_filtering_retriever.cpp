#include "relevance_filtering_retriever.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

RelevanceFilteringRetriever::RelevanceFilteringRetriever(
    Retriever& inner_retriever,
    RelevanceFilteringRetrieverConfig config
)
    : inner_retriever_(inner_retriever)
    , config_(config) {
    validateConfig();
}

void RelevanceFilteringRetriever::validateConfig() const {
    if (!std::isfinite(config_.minimum_sparse_score) || config_.minimum_sparse_score <= 0.0F) {
        throw std::invalid_argument("minimum_sparse_score must be finite and greater than zero");
    }

    if (
        !std::isfinite(config_.minimum_dense_similarity) ||
        config_.minimum_dense_similarity <= 0.0F ||
        config_.minimum_dense_similarity >= 1.0F
    ) {
        throw std::invalid_argument(
            "minimum_dense_similarity must be finite and between zero and one"
        );
    }

    if (config_.candidate_top_k <= 0) {
        throw std::invalid_argument(
            "candidate_top_k must be greater than zero"
        );
    }
}

bool RelevanceFilteringRetriever::isRelevant(
    const RetrievalResult& result
) const noexcept {
    const bool sparse_pass = result.sparse_score >= config_.minimum_sparse_score;

    const bool dense_pass = result.dense_score >= config_.minimum_dense_similarity;

    return sparse_pass || dense_pass;
}

std::vector<RetrievalResult> RelevanceFilteringRetriever::searchTopK(
    const std::string& query,
    int top_k
) const {
    if (query.empty() || top_k <= 0) {
        return {};
    }

    const int candidate_top_k = std::max(top_k, config_.candidate_top_k);

    std::vector<RetrievalResult> candidates = inner_retriever_.searchTopK(
        query, candidate_top_k
    );

    std::vector<RetrievalResult> results;
    results.reserve(std::min(candidates.size(), static_cast<std::size_t>(top_k)));

    /*
     * inner_retriever 已经按照自己的 final_score 排序。
     *
     * 这里仅过滤，不重新排序，
     * 所以保留原检索器的相对排名。
     */
    for (RetrievalResult& candidate : candidates) {
        if (!isRelevant(candidate)) {
            continue;
        }

        results.push_back(std::move(candidate));

        if (results.size() == static_cast<std::size_t>(top_k)) {
            break;
        }
    }

    return results;
}
