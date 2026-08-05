#include "retriever_runtime.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testBm25Runtime(const std::string& knowledge_path) {
    RetrieverRuntimeConfig config;
    config.backend = "bm25";
    config.knowledge_path = knowledge_path;

    RetrieverRuntime runtime(config);

    expect(
        runtime.load(),
        "BM25 runtime failed: " + runtime.lastError()
    );

    expect(
        runtime.backendName() == "bm25",
        "unexpected BM25 backend name"
    );

    const std::vector<RetrievalResult> results =
        runtime.retriever().searchTopK(
            "空调怎么打开",
            3
        );

    expect(
        !results.empty(),
        "BM25 runtime returned no result"
    );

    expect(
        results.front().chunk.chunk_id == 0,
        "BM25 runtime returned unexpected top chunk"
    );
}

void testUnsupportedBackend(
    const std::string& knowledge_path) {

    RetrieverRuntimeConfig config;
    config.backend = "invalid";
    config.knowledge_path = knowledge_path;

    RetrieverRuntime runtime(config);

    expect(
        !runtime.load(),
        "unsupported backend should fail"
    );

    expect(
        runtime.lastError().find(
            "unsupported retrieval backend"
        ) != std::string::npos,
        "unsupported backend error is missing"
    );
}

void testUnloadedAccess() {
    RetrieverRuntimeConfig config;
    RetrieverRuntime runtime(config);

    bool thrown = false;

    try {
        runtime.retriever();
    } catch (const std::logic_error&) {
        thrown = true;
    }

    expect(
        thrown,
        "access before load should throw"
    );
}

void enableRelevanceFilter(
    RetrieverRuntimeConfig& config
) {
    config.relevance_filter_enabled = true;

    config.relevance_minimum_sparse_score =
        6.0F;

    config.relevance_minimum_dense_similarity =
        0.40F;

    config.relevance_candidate_top_k = 20;
}

void testFilteredBm25Runtime(
    const std::string& knowledge_path
) {
    RetrieverRuntimeConfig config;
    config.backend = "bm25";
    config.knowledge_path = knowledge_path;

    enableRelevanceFilter(config);

    RetrieverRuntime runtime(config);

    expect(
        runtime.load(),
        "filtered BM25 runtime failed: "
            + runtime.lastError()
    );

    expect(
        runtime.relevanceFilterEnabled(),
        "BM25 filter should be enabled"
    );

    const auto positive_results =
        runtime.retriever().searchTopK(
            "车里太热了，怎样凉快一点",
            3
        );

    expect(
        !positive_results.empty(),
        "filtered BM25 should retain "
        "vehicle query"
    );

    expect(
        positive_results.front()
            .chunk.chunk_id == 0,
        "filtered BM25 should retain "
        "air conditioner result"
    );

    const auto negative_results =
        runtime.retriever().searchTopK(
            "今天北京天气怎么样",
            3
        );

    expect(
        negative_results.empty(),
        "filtered BM25 should reject "
        "weather query"
    );
}

#ifdef EDGE_ENABLE_BGE_EMBEDDER

RetrieverRuntimeConfig makeDenseConfig(
    const std::string& knowledge_path,
    const std::string& model_dir,
    const std::string& index_dir) {

    RetrieverRuntimeConfig config;

    config.knowledge_path = knowledge_path;

    config.bge_model_path =
        model_dir + "/model.onnx";

    config.bge_tokenizer_path =
        model_dir + "/tokenizer.json";

    config.dense_index_metadata_path =
        index_dir + "/index_meta.json";

    config.dense_embeddings_path =
        index_dir + "/embeddings.f32";

    config.dense_minimum_similarity = -1.0F;

    return config;
}

void testDenseRuntime(
    const std::string& knowledge_path,
    const std::string& model_dir,
    const std::string& index_dir) {

    RetrieverRuntimeConfig config =
        makeDenseConfig(
            knowledge_path,
            model_dir,
            index_dir
        );

    config.backend = "dense";

    RetrieverRuntime runtime(config);

    expect(
        runtime.load(),
        "Dense runtime failed: " + runtime.lastError()
    );

    const auto results =
        runtime.retriever().searchTopK(
            "冬天座位太凉了怎么办",
            3
        );

    expect(
        !results.empty(),
        "Dense runtime returned no result"
    );

    expect(
        results.front().chunk.chunk_id == 3,
        "Dense runtime returned unexpected top chunk"
    );
}

void testHybridRuntime(
    const std::string& knowledge_path,
    const std::string& model_dir,
    const std::string& index_dir) {

    RetrieverRuntimeConfig config =
        makeDenseConfig(
            knowledge_path,
            model_dir,
            index_dir
        );

    config.backend = "hybrid";

    RetrieverRuntime runtime(config);

    expect(
        runtime.load(),
        "Hybrid runtime failed: " + runtime.lastError()
    );

    const auto results =
        runtime.retriever().searchTopK(
            "下雨后前挡风玻璃看不清怎么办",
            3
        );

    expect(
        !results.empty(),
        "Hybrid runtime returned no result"
    );

    expect(
        results.front().chunk.chunk_id == 5,
        "Hybrid runtime returned unexpected top chunk"
    );
}

void testFilteredHybridRuntime(
    const std::string& knowledge_path,
    const std::string& model_dir,
    const std::string& index_dir
) {
    RetrieverRuntimeConfig config =
        makeDenseConfig(
            knowledge_path,
            model_dir,
            index_dir
        );

    config.backend = "hybrid";

    enableRelevanceFilter(config);

    RetrieverRuntime runtime(config);

    expect(
        runtime.load(),
        "filtered Hybrid runtime failed: "
            + runtime.lastError()
    );

    const auto positive_results =
        runtime.retriever().searchTopK(
            "行李应该放在哪里",
            3
        );

    expect(
        !positive_results.empty(),
        "filtered Hybrid should retain "
        "trunk query"
    );

    expect(
        positive_results.front()
            .chunk.chunk_id == 7,
        "filtered Hybrid should return trunk"
    );

    const std::vector<std::string>
        negative_queries{
            "今天北京天气怎么样",
            "红烧肉应该怎么做",
            "今天股票行情如何",
            "请解释量子纠缠实验",
            "给我讲一个笑话"
        };

    for (
        const std::string& query :
        negative_queries
    ) {
        expect(
            runtime.retriever()
                .searchTopK(query, 3)
                .empty(),
            "filtered Hybrid should reject: "
                + query
        );
    }
}

#else

void testDisabledDenseBackend(
    const std::string& knowledge_path) {

    RetrieverRuntimeConfig config;
    config.backend = "dense";
    config.knowledge_path = knowledge_path;

    RetrieverRuntime runtime(config);

    expect(
        !runtime.load(),
        "Dense should fail when BGE is disabled"
    );

    expect(
        runtime.lastError().find(
            "EDGE_ENABLE_BGE_EMBEDDER=ON"
        ) != std::string::npos,
        "missing BGE disabled error"
    );
}

#endif

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <knowledge.json> <model_dir> <index_dir>\n";

        return 1;
    }

    try {
        const std::string knowledge_path = argv[1];
        const std::string model_dir = argv[2];
        const std::string index_dir = argv[3];

        testBm25Runtime(knowledge_path);
        testUnsupportedBackend(knowledge_path);
        testFilteredBm25Runtime(knowledge_path);
        testUnloadedAccess();

#ifdef EDGE_ENABLE_BGE_EMBEDDER
        testDenseRuntime(
            knowledge_path,
            model_dir,
            index_dir
        );

        testHybridRuntime(
            knowledge_path,
            model_dir,
            index_dir
        );

        testFilteredHybridRuntime(
            knowledge_path,
            model_dir,
            index_dir
        );
#else
        testDisabledDenseBackend(knowledge_path);
#endif

        std::cout
            << "retriever_runtime_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "retriever_runtime_tests failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
