#include "bm25_retriever.h"
#include "dense_retriever.h"
#include "hybrid_retriever.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using edge_voice_rag::HybridRetriever;
using edge_voice_rag::HybridRetrieverConfig;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectTopChunk(
    const HybridRetriever& retriever,
    const std::string& query,
    int expected_chunk_id) {

    const std::vector<RetrievalResult> results =
        retriever.searchTopK(query, 3);

    expect(!results.empty(),
           "no result for query: " + query);

    expect(
        results.front().chunk.chunk_id == expected_chunk_id,
        "unexpected top result for query: " + query +
            ", actual chunk_id=" +
            std::to_string(results.front().chunk.chunk_id));
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr
            << "Usage: " << argv[0]
            << " <knowledge.json> <bge_model_dir> <index_dir>\n";
        return 1;
    }

    try {
        const std::string knowledge_path = argv[1];
        const std::string model_dir = argv[2];
        const std::string index_dir = argv[3];

        Bm25Retriever sparse_retriever(knowledge_path);
        expect(
            sparse_retriever.loadKnowledgeBase(),
            "failed to load sparse retriever");

        DenseRetrieverConfig dense_config;
        dense_config.knowledge_path = knowledge_path;
        dense_config.index_metadata_path =
            index_dir + "/index_meta.json";
        dense_config.embeddings_path =
            index_dir + "/embeddings.f32";
        dense_config.embedder.model_path =
            model_dir + "/model.onnx";
        dense_config.embedder.tokenizer_path =
            model_dir + "/tokenizer.json";
        dense_config.minimum_similarity = -1.0F;

        DenseRetriever dense_retriever(dense_config);

        const bool dense_loaded =
            dense_retriever.load();

        expect(
            dense_loaded,
            "failed to load dense retriever: " +
                dense_retriever.lastError());

        HybridRetrieverConfig hybrid_config;
        hybrid_config.rrf_k = 60.0F;
        hybrid_config.sparse_weight = 1.0F;
        hybrid_config.dense_weight = 1.0F;
        hybrid_config.candidate_top_k = 20;

        HybridRetriever hybrid_retriever(
            sparse_retriever,
            dense_retriever,
            hybrid_config);

        expectTopChunk(
            hybrid_retriever,
            "空调怎么打开",
            0);

        expectTopChunk(
            hybrid_retriever,
            "冬天座位太凉了怎么办",
            3);

        expectTopChunk(
            hybrid_retriever,
            "下雨后前挡风玻璃看不清怎么办",
            5);

        expectTopChunk(
            hybrid_retriever,
            "后备箱怎么打开",
            7);

        const auto air_conditioner_results =
            hybrid_retriever.searchTopK("空调怎么打开", 3);

        expect(
            air_conditioner_results.front().sparse_score > 0.0F,
            "expected a preserved BM25 score");

        expect(
            air_conditioner_results.front().dense_score > 0.0F,
            "expected a preserved dense score");

        expect(
            air_conditioner_results.front().final_score > 0.0F,
            "expected a positive RRF score");

        std::cout
            << "hybrid_retriever_integration_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "hybrid_retriever_integration_tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
