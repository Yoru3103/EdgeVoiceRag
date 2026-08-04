#pragma once

#include <string>
#include <vector>

#include "bge_embedder.h"
#include "dense_vector_index.h"
#include "retriever.h"

struct DenseRetrieverConfig {
    std::string knowledge_path;

    std::string index_metadata_path;
    std::string embeddings_path;

    BgeEmbedderConfig embedder;

    /*
     * 暂时不使用严格阈值。
     *
     * BGE不同领域的相似度分布不同，
     * 要先评测再确定阈值。
     */
    float minimum_similarity = -1.0F;
};

class DenseRetriever final : public Retriever {
public:
    explicit DenseRetriever(DenseRetrieverConfig config);

    bool load();

    std::vector<RetrievalResult> searchTopK(
        const std::string& query,
        int top_k
    ) const override;

    bool isLoaded() const noexcept;

    const std::string& lastError() const noexcept;

private:
    DenseRetrieverConfig config_;

    BgeEmbedder embedder_;
    DenseVectorIndex index_;

    std::vector<DocumentChunk> documents_;

    bool loaded_ = false;
    std::string last_error_;
};
