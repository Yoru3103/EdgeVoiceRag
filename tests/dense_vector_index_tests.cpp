#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "dense_vector_index.h"

using Json = nlohmann::json;

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

void expectNear(
    float actual,
    float expected,
    float tolerance,
    const std::string& test_name
) {
    expectTrue(
        std::abs(actual - expected) <= tolerance,
        test_name
    );
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto unique_value =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();

        path_ =
            std::filesystem::temp_directory_path() /
            (
                "edge_voice_rag_dense_index_" +
                std::to_string(unique_value)
            );

        std::filesystem::create_directories(
            path_
        );
    }

    ~TemporaryDirectory() {
        std::error_code error;

        std::filesystem::remove_all(
            path_,
            error
        );
    }

    const std::filesystem::path& path()
        const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeMetadata(
    const std::filesystem::path& path,
    std::size_t rows,
    std::size_t dimension,
    const std::vector<int>& chunk_ids,
    bool normalized = true
) {
    Json metadata{
        {
            "format",
            "edge_voice_rag_dense_index"
        },
        {
            "version",
            2
        },
        {
            "model_id",
            "test/embedding-model"
        },
        {
            "pooling",
            "cls"
        },
        {
            "normalized",
            normalized
        },
        {
            "dtype",
            "float32_le"
        },
        {
            "dimension",
            dimension
        },
        {
            "rows",
            rows
        },
        {
            "max_length",
            32
        },
        {
            "query_instruction",
            "测试检索指令："
        },
        {
            "chunks_fnv1a64",
            "0123456789abcdef"
        },
        {
            "chunk_ids",
            chunk_ids
        }
    };

    std::ofstream output(path);
    output << metadata.dump(2) << '\n';
}

void writeEmbeddings(
    const std::filesystem::path& path,
    const std::vector<float>& embeddings
) {
    std::ofstream output(
        path,
        std::ios::binary
    );

    output.write(
        reinterpret_cast<const char*>(
            embeddings.data()
        ),
        static_cast<std::streamsize>(
            embeddings.size() *
            sizeof(float)
        )
    );
}

void writeValidIndex(
    const std::filesystem::path& directory
) {
    writeMetadata(
        directory / "index_meta.json",
        3,
        3,
        {10, 20, 30}
    );

    /*
     * 三个单位向量：
     *
     * chunk 10 -> X轴
     * chunk 20 -> Y轴
     * chunk 30 -> Z轴
     */
    writeEmbeddings(
        directory / "embeddings.f32",
        {
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 1.0F
        }
    );
}

DenseVectorIndex createIndex(
    const std::filesystem::path& directory
) {
    return DenseVectorIndex(
        (
            directory /
            "index_meta.json"
        ).string(),
        (
            directory /
            "embeddings.f32"
        ).string()
    );
}

void testLoadValidIndex() {
    TemporaryDirectory temporary;
    writeValidIndex(temporary.path());

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        index.load(),
        "DenseVectorIndex: load valid index"
    );

    expectTrue(
        index.isLoaded(),
        "DenseVectorIndex: loaded state"
    );

    expectTrue(
        index.rowCount() == 3,
        "DenseVectorIndex: row count"
    );

    expectTrue(
        index.dimension() == 3,
        "DenseVectorIndex: dimension"
    );

    expectTrue(
        index.maxLength() == 32,
        "DenseVectorIndex: max length"
    );

    expectTrue(
        index.chunkIds() ==
            std::vector<int>({10, 20, 30}),
        "DenseVectorIndex: chunk ID mapping"
    );

    expectTrue(
        index.modelId() ==
            "test/embedding-model",
        "DenseVectorIndex: model ID"
    );

    expectTrue(
        index.queryInstruction() ==
            "测试检索指令：",
        "DenseVectorIndex: query instruction"
    );

    expectTrue(
        index.chunksFingerprint()
            == "0123456789abcdef",
        "DenseVectorIndex: "
        "chunks fingerprint"
    );
}

void testExactVectorSearch() {
    TemporaryDirectory temporary;
    writeValidIndex(temporary.path());

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        index.load(),
        "DenseVectorIndex: load for exact search"
    );

    const std::vector<DenseVectorSearchHit> results =
        index.searchTopK(
            {1.0F, 0.0F, 0.0F},
            3
        );

    expectTrue(
        results.size() == 3,
        "DenseVectorIndex: exact search result count"
    );

    if (!results.empty()) {
        expectTrue(
            results[0].chunk_id == 10,
            "DenseVectorIndex: X vector ranks chunk 10 first"
        );

        expectNear(
            results[0].score,
            1.0F,
            0.0001F,
            "DenseVectorIndex: exact cosine score"
        );
    }
}

void testUnnormalizedQuery() {
    TemporaryDirectory temporary;
    writeValidIndex(temporary.path());

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        index.load(),
        "DenseVectorIndex: load for unnormalized query"
    );

    const std::vector<DenseVectorSearchHit> results =
        index.searchTopK(
            {0.0F, 5.0F, 0.0F},
            1
        );

    expectTrue(
        results.size() == 1,
        "DenseVectorIndex: unnormalized query result"
    );

    if (!results.empty()) {
        expectTrue(
            results[0].chunk_id == 20,
            "DenseVectorIndex: Y vector ranks chunk 20 first"
        );

        expectNear(
            results[0].score,
            1.0F,
            0.0001F,
            "DenseVectorIndex: unnormalized cosine score"
        );
    }
}

void testTopKAndTieBreaking() {
    TemporaryDirectory temporary;
    writeValidIndex(temporary.path());

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        index.load(),
        "DenseVectorIndex: load for Top-K"
    );

    const std::vector<DenseVectorSearchHit> results =
        index.searchTopK(
            {1.0F, 1.0F, 0.0F},
            2
        );

    expectTrue(
        results.size() == 2,
        "DenseVectorIndex: Top-K limit"
    );

    if (results.size() == 2) {
        expectTrue(
            results[0].chunk_id == 10,
            "DenseVectorIndex: tie uses lower chunk ID first"
        );

        expectTrue(
            results[1].chunk_id == 20,
            "DenseVectorIndex: tie returns second chunk"
        );
    }
}

void testInvalidQueries() {
    TemporaryDirectory temporary;
    writeValidIndex(temporary.path());

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        index.load(),
        "DenseVectorIndex: load for invalid queries"
    );

    expectTrue(
        index.searchTopK(
            {1.0F, 0.0F},
            3
        ).empty(),
        "DenseVectorIndex: reject wrong query dimension"
    );

    expectTrue(
        index.searchTopK(
            {0.0F, 0.0F, 0.0F},
            3
        ).empty(),
        "DenseVectorIndex: reject zero vector"
    );

    expectTrue(
        index.searchTopK(
            {1.0F, 0.0F, 0.0F},
            0
        ).empty(),
        "DenseVectorIndex: reject non-positive Top-K"
    );
}

void testWrongBinarySize() {
    TemporaryDirectory temporary;

    writeMetadata(
        temporary.path() /
            "index_meta.json",
        3,
        3,
        {10, 20, 30}
    );

    /*
     * 元数据要求9个float，这里只写3个。
     */
    writeEmbeddings(
        temporary.path() /
            "embeddings.f32",
        {
            1.0F,
            0.0F,
            0.0F
        }
    );

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        !index.load(),
        "DenseVectorIndex: reject truncated binary"
    );

    expectTrue(
        index.lastError().find(
            "size mismatch"
        ) != std::string::npos,
        "DenseVectorIndex: truncated binary error"
    );
}

void testDuplicateChunkIds() {
    TemporaryDirectory temporary;

    writeMetadata(
        temporary.path() /
            "index_meta.json",
        3,
        3,
        {10, 10, 30}
    );

    writeEmbeddings(
        temporary.path() /
            "embeddings.f32",
        {
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 1.0F
        }
    );

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        !index.load(),
        "DenseVectorIndex: reject duplicate chunk IDs"
    );
}

void testNonNormalizedDocumentVector() {
    TemporaryDirectory temporary;

    writeMetadata(
        temporary.path() /
            "index_meta.json",
        1,
        3,
        {10}
    );

    writeEmbeddings(
        temporary.path() /
            "embeddings.f32",
        {
            2.0F,
            0.0F,
            0.0F
        }
    );

    DenseVectorIndex index =
        createIndex(temporary.path());

    expectTrue(
        !index.load(),
        "DenseVectorIndex: reject non-normalized vector"
    );
}

} // namespace

int main() {
    testLoadValidIndex();
    testExactVectorSearch();
    testUnnormalizedQuery();
    testTopKAndTieBreaking();
    testInvalidQueries();
    testWrongBinarySize();
    testDuplicateChunkIds();
    testNonNormalizedDocumentVector();

    if (g_failed_count == 0) {
        std::cout
            << "All DenseVectorIndex tests passed."
            << '\n';

        return 0;
    }

    std::cerr
        << g_failed_count
        << " DenseVectorIndex test(s) failed."
        << '\n';

    return 1;
}
