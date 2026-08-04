#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "bge_embedder.h"
#include "dense_vector_index.h"

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

float vectorNorm(
    const std::vector<float>& values
) {
    double squared_norm = 0.0;

    for (const float value : values) {
        squared_norm +=
            static_cast<double>(value) *
            static_cast<double>(value);
    }

    return static_cast<float>(
        std::sqrt(squared_norm)
    );
}

float cosineSimilarity(
    const std::vector<float>& left,
    const std::vector<float>& right
) {
    if (
        left.empty() ||
        left.size() != right.size()
    ) {
        return 0.0F;
    }

    double dot_product = 0.0;
    double left_squared_norm = 0.0;
    double right_squared_norm = 0.0;

    for (
        std::size_t index = 0;
        index < left.size();
        ++index
    ) {
        dot_product +=
            static_cast<double>(left[index]) *
            static_cast<double>(right[index]);

        left_squared_norm +=
            static_cast<double>(left[index]) *
            static_cast<double>(left[index]);

        right_squared_norm +=
            static_cast<double>(right[index]) *
            static_cast<double>(right[index]);
    }

    return static_cast<float>(
        dot_product /
        (
            std::sqrt(left_squared_norm) *
            std::sqrt(right_squared_norm)
        )
    );
}

} // namespace

int main(
    int argc,
    char* argv[]
) {
    if (argc != 3) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <model-dir> <index-dir>\n";

        return 1;
    }

    const std::string model_dir = argv[1];
    const std::string index_dir = argv[2];

    BgeEmbedderConfig config;
    config.model_path =
        model_dir + "/model.onnx";

    config.tokenizer_path =
        model_dir + "/tokenizer.json";

    config.intra_op_threads = 2;
    config.inter_op_threads = 1;

    BgeEmbedder embedder(config);

    expectTrue(
        embedder.load(),
        "BgeEmbedder: load model and tokenizer"
    );

    if (!embedder.isLoaded()) {
        std::cerr
            << "BGE load error: "
            << embedder.lastError()
            << '\n';

        return 1;
    }

    const BgeEmbeddingResult first =
        embedder.encodeQuery(
            "空调怎么打开"
        );

    expectTrue(
        first.ok,
        "BgeEmbedder: encode query"
    );

    if (!first.ok) {
        std::cerr
            << "BGE encode error: "
            << first.error
            << '\n';

        return 1;
    }

    expectTrue(
        first.embedding.size() == 512,
        "BgeEmbedder: output dimension"
    );

    expectTrue(
        std::abs(
            vectorNorm(first.embedding) -
            1.0F
        ) < 0.0001F,
        "BgeEmbedder: output is normalized"
    );

    const BgeEmbeddingResult second =
        embedder.encodeQuery(
            "空调怎么打开"
        );

    expectTrue(
        second.ok,
        "BgeEmbedder: repeated encode"
    );

    if (second.ok) {
        expectTrue(
            cosineSimilarity(
                first.embedding,
                second.embedding
            ) > 0.99999F,
            "BgeEmbedder: deterministic output"
        );
    }

    const BgeEmbeddingResult empty =
        embedder.encodeQuery("");

    expectTrue(
        !empty.ok,
        "BgeEmbedder: reject empty query"
    );

    DenseVectorIndex index(
        index_dir + "/index_meta.json",
        index_dir + "/embeddings.f32"
    );

    expectTrue(
        index.load(),
        "BgeEmbedder: load real dense index"
    );

    if (!index.isLoaded()) {
        std::cerr
            << "Dense index error: "
            << index.lastError()
            << '\n';

        return 1;
    }

    expectTrue(
        index.dimension() ==
            first.embedding.size(),
        "BgeEmbedder: model/index dimensions match"
    );

    const std::vector<DenseVectorSearchHit>
        search_results =
            index.searchTopK(
                first.embedding,
                3
            );

    expectTrue(
        !search_results.empty(),
        "BgeEmbedder: dense search result"
    );

    if (!search_results.empty()) {
        expectTrue(
            search_results.front().chunk_id == 0,
            "BgeEmbedder: air conditioner ranks first"
        );

        std::cout
            << "Top dense score: "
            << search_results.front().score
            << '\n';
    }

    if (g_failed_count == 0) {
        std::cout
            << "All BgeEmbedder tests passed."
            << '\n';

        return 0;
    }

    std::cerr
        << g_failed_count
        << " BgeEmbedder test(s) failed."
        << '\n';

    return 1;
}
