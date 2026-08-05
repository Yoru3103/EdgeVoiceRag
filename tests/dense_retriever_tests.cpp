#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include "dense_retriever.h"

namespace {

int g_failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& test_name
) {
    if (condition) {
        std::cout
            << "[PASS] "
            << test_name
            << '\n';
    } else {
        std::cerr
            << "[FAIL] "
            << test_name
            << '\n';

        ++g_failed_count;
    }
}

void expectTopChunk(
    const DenseRetriever& retriever,
    const std::string& query,
    int expected_chunk_id,
    const std::string& test_name
) {
    const std::vector<RetrievalResult> results =
        retriever.searchTopK(
            query,
            3
        );

    expectTrue(
        !results.empty(),
        test_name + ": result is not empty"
    );

    if (results.empty()) {
        return;
    }

    expectTrue(
        results.front().chunk.chunk_id ==
            expected_chunk_id,
        test_name + ": expected Top-1 chunk"
    );

    expectTrue(
        results.front().dense_score > 0.0F,
        test_name + ": dense score is positive"
    );

    expectTrue(
        results.front().sparse_score == 0.0F,
        test_name + ": sparse score is zero"
    );

    expectTrue(
        std::abs(
            results.front().final_score -
            results.front().dense_score
        ) < 0.0001F,
        test_name + ": final score equals dense score"
    );
}

} // namespace

int main(
    int argc,
    char* argv[]
) {
    if (argc != 4) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <chunks.json>"
            << " <model-dir>"
            << " <index-dir>\n";

        return 1;
    }

    const std::string chunks_path =
        argv[1];

    const std::string model_dir =
        argv[2];

    const std::string index_dir =
        argv[3];

    DenseRetrieverConfig config;

    config.knowledge_path =
        chunks_path;

    config.index_metadata_path =
        index_dir + "/index_meta.json";

    config.embeddings_path =
        index_dir + "/embeddings.f32";

    config.embedder.model_path =
        model_dir + "/model.onnx";

    config.embedder.tokenizer_path =
        model_dir + "/tokenizer.json";

    config.embedder.intra_op_threads = 2;
    config.embedder.inter_op_threads = 1;

    DenseRetriever retriever(
        std::move(config)
    );

    expectTrue(
        retriever.load(),
        "DenseRetriever: load"
    );

    if (!retriever.isLoaded()) {
        std::cerr
            << "DenseRetriever load error: "
            << retriever.lastError()
            << '\n';

        return 1;
    }

    expectTopChunk(
        retriever,
        "空调怎么打开",
        0,
        "DenseRetriever: air conditioner"
    );

    /*
     * 以下三条都是原BM25基线失败的语义查询。
     */
    expectTopChunk(
        retriever,
        "冬天座位太凉了怎么办",
        3,
        "DenseRetriever: semantic seat heating"
    );

    expectTopChunk(
        retriever,
        "下雨后前挡风玻璃看不清怎么办",
        5,
        "DenseRetriever: semantic wiper"
    );

    expectTopChunk(
        retriever,
        "行李应该放在哪里",
        7,
        "DenseRetriever: semantic trunk"
    );

    expectTopChunk(
        retriever,
        "车里太热了，怎样凉快一点",
        0,
        "DenseRetriever: semantic air conditioner"
    );

    const std::vector<RetrievalResult> top_two =
        retriever.searchTopK(
            "手机如何与车辆进行配对",
            2
        );

    expectTrue(
        top_two.size() == 2,
        "DenseRetriever: respect Top-K"
    );

    expectTrue(
        retriever.searchTopK(
            "",
            3
        ).empty(),
        "DenseRetriever: reject empty query"
    );

    expectTrue(
        retriever.searchTopK(
            "空调怎么打开",
            0
        ).empty(),
        "DenseRetriever: reject non-positive Top-K"
    );

    if (g_failed_count == 0) {
        std::cout
            << "All DenseRetriever tests passed."
            << '\n';

        return 0;
    }

    std::cerr
        << g_failed_count
        << " DenseRetriever test(s) failed."
        << '\n';

    return 1;
}
