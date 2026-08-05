#pragma once

#include <memory>
#include <string>

#include "retriever.h"

struct RetrieverRuntimeConfig {
    // bm25, dense, hybrid
    std::string backend = "bm25";

    std::string knowledge_path = "vector_db/chunks.json";

    std::string bge_model_path = "models/embedding/bge-small-zh-v1.5/model.onnx";

    std::string bge_tokenizer_path = "models/embedding/bge-small-zh-v1.5/tokenizer.json";

    std::string dense_index_metadata_path = "vector_db/bge-small-zh-v1.5/index_meta.json";

    std::string dense_embeddings_path = "vector_db/bge-small-zh-v1.5/embeddings.f32";

    float dense_minimum_similarity = -1.0F;

    float hybrid_rrf_k = 60.0F;
    float hybrid_sparse_weight = 1.0F;
    float hybrid_dense_weight = 1.0F;

    int hybrid_candidate_top_k = 20;

    // 默认关闭
    bool relevance_filter_enabled = false;

    float relevance_minimum_sparse_score = 6.0F;
    float relevance_minimum_dense_similarity = 0.40F;

    int relevance_candidate_top_k = 20;
};

class RetrieverRuntime final {
public:
    explicit RetrieverRuntime(RetrieverRuntimeConfig config);

    ~RetrieverRuntime();

    RetrieverRuntime(const RetrieverRuntime&) = delete;
    RetrieverRuntime& operator=(const RetrieverRuntime&) = delete;

    bool load();
    bool relevanceFilterEnabled() const noexcept;

    Retriever& retriever();
    const Retriever& retriever() const;

    const std::string& backendName() const noexcept;
    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
