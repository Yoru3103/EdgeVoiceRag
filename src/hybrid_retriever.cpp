#include "hybrid_retriever.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace edge_voice_rag {
namespace {

struct Candidate {
    RetrievalResult result;
};

float calculateRrfContribution(
    float weight,
    float rrf_k,
    std::size_t zero_based_rank
) {
    const float rank = static_cast<float>(zero_based_rank + 1);

    return weight / (rrf_k + rank);
}
}   // namespace

HybridRetriever::HybridRetriever(
    Retriever& sparse_retriever,
    Retriever& dense_retriever,
    HybridRetrieverConfig config
)   : sparse_retriever_(sparse_retriever)
    , dense_retriever_(dense_retriever)
    , config_(std::move(config)) {
    validateConfig();
}

void HybridRetriever::validateConfig() const {
    if (!std::isfinite(config_.rrf_k) || config_.rrf_k <= 0.0F) {
        throw std::invalid_argument(
            "HybridRetriever rrf_k must be finite and greater than zero");
    }

    if (!std::isfinite(config_.sparse_weight) ||
        config_.sparse_weight < 0.0F) {
        throw std::invalid_argument(
            "HybridRetriever sparse_weight must be finite and non-negative");
    }

    if (!std::isfinite(config_.dense_weight) ||
        config_.dense_weight < 0.0F) {
        throw std::invalid_argument(
            "HybridRetriever dense_weight must be finite and non-negative");
    }

    if (config_.sparse_weight == 0.0F &&
        config_.dense_weight == 0.0F) {
        throw std::invalid_argument(
            "HybridRetriever weights cannot both be zero");
    }

    if (config_.candidate_top_k <= 0) {
        throw std::invalid_argument(
            "HybridRetriever candidate_top_k must be greater than zero");
    }
}

std::vector<RetrievalResult> HybridRetriever::searchTopK(
    const std::string& query,
    int top_k
) const {
    if (query.empty() || top_k <= 0) {
        return {};
    }

    // 最终只返回 top_k，但融合时应获取更多候选文档。
    const int candidate_top_k = std::max(top_k, config_.candidate_top_k);

    const std::vector<RetrievalResult> sparse_results = sparse_retriever_.searchTopK(query, candidate_top_k);
    const std::vector<RetrievalResult> dense_results = dense_retriever_.searchTopK(query, candidate_top_k);

    std::unordered_map<int, Candidate> candidates;
    candidates.reserve(sparse_results.size() + dense_results.size());

    for (std::size_t rank = 0; rank < sparse_results.size(); rank++) {
        const RetrievalResult& source = sparse_results[rank];
        Candidate& candidate = candidates[source.chunk.chunk_id];

        candidate.result.chunk = source.chunk;
        candidate.result.sparse_score = source.sparse_score;

        candidate.result.final_score += calculateRrfContribution(
            config_.sparse_weight,
            config_.rrf_k,
            rank
        );
    }

    for (std::size_t rank = 0; rank < dense_results.size(); rank++) {
        const RetrievalResult& source = dense_results[rank];
        Candidate& candidate = candidates[source.chunk.chunk_id];

        candidate.result.chunk = source.chunk;
        candidate.result.dense_score = source.dense_score;

        candidate.result.final_score += calculateRrfContribution(
            config_.dense_weight,
            config_.rrf_k,
            rank
        );
    }

    std::vector<RetrievalResult> results;
    results.reserve(candidates.size());

    for (auto& entry : candidates) {
        results.push_back(std::move(entry.second.result));
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const RetrievalResult& left, const RetrievalResult& right) {
            if (left.final_score != right.final_score) {
                return left.final_score > right.final_score;
            }

            // 分数相同时保证结果顺序稳定，方便测试和复现。
            return left.chunk.chunk_id < right.chunk.chunk_id;
        }
    );

    if (results.size() > static_cast<std::size_t>(top_k)) {
        results.resize(static_cast<std::size_t>(top_k));
    }

    return results;
}

}   // namespace edge_voice_rag
