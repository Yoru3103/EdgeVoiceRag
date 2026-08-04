#pragma once

#include "retriever.h"

#include <string>
#include <vector>

namespace edge_voice_rag {

struct HybridRetrieverConfig {
    // RRF 平滑参数。通常使用 60。
    float rrf_k = 60.0F;

    // BM25 排名在融合结果中的权重。
    float sparse_weight = 1.0F;

    // 向量检索排名在融合结果中的权重。
    float dense_weight = 1.0F;

    // 每个子检索器参与融合的候选数量。
    int candidate_top_k = 20;
};

class HybridRetriever final : public Retriever {
public:
    HybridRetriever(
        Retriever& sparse_retriever,
        Retriever& dense_retriever,
        HybridRetrieverConfig config = {}
    );

    std::vector<RetrievalResult> searchTopK(
        const std::string& query,
        int top_k
    ) const override;

private:
    void validateConfig() const;

    Retriever& sparse_retriever_;
    Retriever& dense_retriever_;
    HybridRetrieverConfig config_;
};

}   // namespace edge_voice_rag
