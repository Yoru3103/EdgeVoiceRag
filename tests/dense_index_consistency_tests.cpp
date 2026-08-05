#include "dense_retriever.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace {

namespace fs = std::filesystem;

class TemporaryChunksFile final {
public:
    explicit TemporaryChunksFile(
        const std::string& source_path
    ) {
        const auto unique_value =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();

        path_ =
            fs::temp_directory_path()
            / (
                "edge_voice_rag_stale_chunks_"
                + std::to_string(unique_value)
                + ".json"
            );

        std::ifstream source(
            source_path,
            std::ios::binary
        );

        if (!source.is_open()) {
            throw std::runtime_error(
                "failed to open source chunks"
            );
        }

        std::ofstream destination(
            path_,
            std::ios::binary
        );

        if (!destination.is_open()) {
            throw std::runtime_error(
                "failed to create temporary chunks"
            );
        }

        destination << source.rdbuf();

        /*
         * JSON 末尾增加空白仍然是合法 JSON，
         * 但文件指纹一定发生变化。
         */
        destination << '\n';

        if (!destination) {
            throw std::runtime_error(
                "failed to write temporary chunks"
            );
        }
    }

    ~TemporaryChunksFile() {
        std::error_code error;
        fs::remove(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

DenseRetrieverConfig makeConfig(
    const std::string& knowledge_path,
    const std::string& model_dir,
    const std::string& index_dir
) {
    DenseRetrieverConfig config;

    config.knowledge_path =
        knowledge_path;

    config.index_metadata_path =
        index_dir + "/index_meta.json";

    config.embeddings_path =
        index_dir + "/embeddings.f32";

    config.embedder.model_path =
        model_dir + "/model.onnx";

    config.embedder.tokenizer_path =
        model_dir + "/tokenizer.json";

    config.minimum_similarity = -1.0F;

    return config;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <chunks.json>"
            << " <model-dir>"
            << " <index-dir>\n";

        return 1;
    }

    try {
        const std::string chunks_path = argv[1];
        const std::string model_dir = argv[2];
        const std::string index_dir = argv[3];

        /*
         * 首先验证正常文件可以加载。
         */
        DenseRetriever valid_retriever(
            makeConfig(
                chunks_path,
                model_dir,
                index_dir
            )
        );

        expect(
            valid_retriever.load(),
            "valid dense index failed: "
                + valid_retriever.lastError()
        );

        /*
         * 修改 chunks 文件，但继续使用原索引。
         */
        TemporaryChunksFile modified_chunks(
            chunks_path
        );

        DenseRetriever stale_retriever(
            makeConfig(
                modified_chunks.path().string(),
                model_dir,
                index_dir
            )
        );

        expect(
            !stale_retriever.load(),
            "stale dense index should fail"
        );

        expect(
            stale_retriever.lastError().find(
                "dense index is stale"
            ) != std::string::npos,
            "missing stale index error"
        );

        std::cout
            << "dense_index_consistency_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "dense_index_consistency_tests failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
