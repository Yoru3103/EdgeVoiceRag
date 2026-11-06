#include "retriever_runtime.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include "bm25_retriever.h"
#include "relevance_filtering_retriever.h"

#ifdef EDGE_ENABLE_BGE_EMBEDDER
#include "dense_retriever.h"
#include "hybrid_retriever.h"
#endif // EDGE_ENABLE_BGE_EMBEDDER

struct RetrieverRuntime::Impl {
    explicit Impl(RetrieverRuntimeConfig runtime_config)
        : config(std::move(runtime_config)){}

    void clear() {
        active_retriever = nullptr;

        relevance_filter.reset();   // 先销毁

        // hybrid本身包含dense和bm25，因此需要先reset hybrid再reset剩余retriever
#ifdef EDGE_ENABLE_BGE_EMBEDDER
        hybrid_retriever.reset();
        dense_retriever.reset();
#endif // EDGE_ENABLE_BGE_EMBEDDER

        bm25_retriever.reset();     
    }

    void activateRetriever(Retriever& base_retriever) {
        if (!config.relevance_filter_enabled) {
            active_retriever = &base_retriever;

            return;
        }

        RelevanceFilteringRetrieverConfig filter_config;
        filter_config.minimum_sparse_score = config.relevance_minimum_sparse_score;
        filter_config.minimum_dense_similarity = config.relevance_minimum_dense_similarity;
        filter_config.candidate_top_k = config.relevance_candidate_top_k;

        relevance_filter = std::make_unique<RelevanceFilteringRetriever>(
            base_retriever,
            filter_config
        );

        active_retriever = relevance_filter.get();
    }
    
    RetrieverRuntimeConfig config;

    std::unique_ptr<Bm25Retriever> bm25_retriever;

#ifdef EDGE_ENABLE_BGE_EMBEDDER
    std::unique_ptr<DenseRetriever> dense_retriever;

    std::unique_ptr<edge_voice_rag::HybridRetriever> hybrid_retriever;
#endif // EDGE_ENABLE_BGE_EMBEDDER

    std::unique_ptr<RelevanceFilteringRetriever> relevance_filter;
    Retriever* active_retriever = nullptr;

    std::string last_error;
};

RetrieverRuntime::RetrieverRuntime(RetrieverRuntimeConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RetrieverRuntime::~RetrieverRuntime() = default;

bool RetrieverRuntime::load() {
    impl_->clear();
    impl_->last_error.clear();

    const std::string& backend = impl_->config.backend;

    if (
        backend != "bm25"
        && backend != "dense"
        && backend != "hybrid"
    ) {
        impl_->last_error =
            "unsupported retrieval backend: " + backend;

        return false;
    }

    if (impl_->config.knowledge_path.empty()) {
        impl_->last_error = "retrieval knowledge_path must not be empty";

        return false;
    }

    try {
        if (backend == "bm25" || backend == "hybrid") {
            impl_->bm25_retriever = std::make_unique<Bm25Retriever>(impl_->config.knowledge_path);
            
            if (!impl_->bm25_retriever->loadKnowledgeBase()) {
                impl_->last_error = "failed to load BM25 knowledge base: "
                    + impl_->config.knowledge_path;

                impl_->clear();
                return false;
            }
        }

        if (backend == "bm25") {
            impl_->activateRetriever(*impl_->bm25_retriever);

            return true;
        }

#ifndef EDGE_ENABLE_BGE_EMBEDDER
        impl_->last_error = "retrieval backend '" + backend
            + "' requires EDGE_ENABLE_BGE_EMBEDDER=ON";

        impl_->clear();
        return false;
#else
        if (
            impl_->config.bge_model_path.empty() ||
            impl_->config.bge_tokenizer_path.empty() ||
            impl_->config.dense_index_metadata_path.empty() ||
            impl_->config.dense_embeddings_path.empty()
        ) {
            impl_->last_error = "dense retrieval model and index paths "
                "must not be empty";

            impl_->clear();
            return false;
        }

        DenseRetrieverConfig dense_config;
        dense_config.knowledge_path = impl_->config.knowledge_path;
        dense_config.index_metadata_path = impl_->config.dense_index_metadata_path;
        dense_config.embeddings_path = impl_->config.dense_embeddings_path;
        dense_config.embedder.model_path = impl_->config.bge_model_path;
        dense_config.embedder.tokenizer_path = impl_->config.bge_tokenizer_path;
        dense_config.minimum_similarity = impl_->config.dense_minimum_similarity;
        
        impl_->dense_retriever = std::make_unique<DenseRetriever>(std::move(dense_config));

        if (!impl_->dense_retriever->load()) {
            impl_->last_error = "failed to load DenseRetriever: "
                + impl_->dense_retriever->lastError();

            impl_->clear();
            return false;
        }

        if (backend == "dense") {
            impl_->activateRetriever(*impl_->dense_retriever);

            return true;
        }

        edge_voice_rag::HybridRetrieverConfig hybrid_config;
        hybrid_config.rrf_k = impl_->config.hybrid_rrf_k;
        hybrid_config.sparse_weight = impl_->config.hybrid_sparse_weight;
        hybrid_config.dense_weight = impl_->config.hybrid_dense_weight;
        hybrid_config.candidate_top_k = impl_->config.hybrid_candidate_top_k;

        impl_->hybrid_retriever = std::make_unique<edge_voice_rag::HybridRetriever>(
            *impl_->bm25_retriever,
            *impl_->dense_retriever,
            hybrid_config
        );

        impl_->activateRetriever(*impl_->hybrid_retriever);

        return true;
#endif // !EDGE_ENABLE_BGE_EMBEDDER
    } catch (const std::exception& error) {
        impl_->last_error =
            "failed to initialize retrieval backend '"
            + backend
            + "': "
            + error.what();

        impl_->clear();
        return false;
    }
}

Retriever& RetrieverRuntime::retriever() {
    if (impl_->active_retriever == nullptr) {
        throw std::logic_error(
            "RetrieverRuntime has not been loaded"
        );
    }

    return *impl_->active_retriever;
}

const Retriever& RetrieverRuntime::retriever() const {
    if (impl_->active_retriever == nullptr) {
        throw std::logic_error(
            "RetrieverRuntime has not been loaded"
        );
    }

    return *impl_->active_retriever;
}

const std::string& RetrieverRuntime::backendName() const noexcept {
    return impl_->config.backend;
}

const std::string& RetrieverRuntime::lastError() const noexcept {
    return impl_->last_error;
}

bool RetrieverRuntime::relevanceFilterEnabled() const noexcept {
    return impl_->config.relevance_filter_enabled;
}
