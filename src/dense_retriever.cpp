#include "dense_retriever.h"

#include <cmath>
#include <stdexcept>
#include <utility>

#include "knowledge_base_loader.h"

DenseRetriever::DenseRetriever(DenseRetrieverConfig config)
    : config_(std::move(config))
    , embedder_(config_.embedder)
    , index_(
        config_.index_metadata_path,
        config_.embeddings_path
    ) {
    if (
        !std::isfinite(config_.minimum_similarity) ||
        config_.minimum_similarity < -1.0F ||
        config_.minimum_similarity > 1.0F
    ) {
        throw std::invalid_argument(
            "dense minimum_similarity "
            "must be between -1 and 1"
        );
    }
}

bool DenseRetriever::load() {
    loaded_ = false;
    last_error_.clear();
    documents_.clear();

    KnowledgeBaseLoadResult document_result = loadDocumentChunks(config_.knowledge_path);

    if (!document_result.ok) {
        last_error_ = document_result.error;

        return false;
    }

    if (!index_.load()) {
        last_error_ = "failed to load dense vector index: " + index_.lastError();

        return false;
    }

    if (
        index_.dimension() != static_cast<std::size_t>(config_.embedder.expected_dimension) 
    ) {
        last_error_ = "BGE model dimension does not match dense vector index";

        return false;
    }

    if (
        index_.maxLength() != config_.embedder.max_length
    ) {
        last_error_ =
            "BGE max_length does not match "
            "dense index metadata";

        return false;
    }

    if (
        index_.queryInstruction() != config_.embedder.query_instruction
    ) {
        last_error_ =
            "BGE query instruction does not match "
            "dense index metadata";

        return false;
    }

    /*
     * 强制要求chunks.json顺序与向量行顺序一致。
     *
     * row 0必须对应documents_[0]，
     * 防止chunk ID集合相同但排列不同。
     */
    if (index_.rowCount() != document_result.documents.size()) {
        last_error_ =
            "dense index row count does not match "
            "knowledge base";

        return false;
    }

    const std::vector<int>& index_chunk_ids = index_.chunkIds();

    for (std::size_t row = 0; row < document_result.documents.size(); row++) {
        if (index_chunk_ids[row] != document_result.documents[row].chunk_id) {
            last_error_ =
                "dense index chunk order does not "
                "match knowledge base at row " +
                std::to_string(row);

            return false;
        }
    }

    if (!embedder_.load()) {
        last_error_ =
            "failed to load BGE embedder: " +
            embedder_.lastError();

        return false;
    }

    documents_ = std::move(document_result.documents);

    loaded_ = true;
    last_error_.clear();
    
    return true;
}

std::vector<RetrievalResult> DenseRetriever::searchTopK(
    const std::string& query,
    int top_k
) const {
    std::vector<RetrievalResult> results;

    if (!loaded_ || query.empty() || top_k <= 0) {
        return results;
    }

    const BgeEmbeddingResult embedding_result = embedder_.encodeQuery(query);

    if (!embedding_result.ok) {
        return results;
    }

    const std::vector<DenseVectorSearchHit> hits = index_.searchTopK(embedding_result.embedding, top_k);

    results.reserve(hits.size());

    for (const DenseVectorSearchHit& hit : hits) {
        if (hit.score < config_.minimum_similarity) {
            continue;
        }

        if (hit.row >= documents_.size()) {
            /*
             * load()已经验证过，正常情况下
             * 不应该进入这个分支。
             * 二次保证
             */
            continue;
        }

        const DocumentChunk& document = documents_[hit.row];

        if (document.chunk_id != hit.chunk_id) {
            continue;
        }

        RetrievalResult result;
        result.chunk = document;

        result.sparse_score = 0.0F;
        result.dense_score = hit.score;
        result.final_score = hit.score;

        results.push_back(std::move(result));
    }

    return results;
}

bool DenseRetriever::isLoaded()
    const noexcept {
    return loaded_;
}

const std::string&
DenseRetriever::lastError() const noexcept {
    return last_error_;
}
