#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct BgeEmbedderConfig {
    std::string model_path;
    std::string tokenizer_path;

    std::string query_instruction = "为这个句子生成表示以用于检索相关文章：";

    int max_length = 512;
    int expected_dimension = 512;

    int intra_op_threads = 4;
    int inter_op_threads = 1;
};

struct BgeEmbeddingTiming {
    bool observed = false;
    std::size_t token_count = 0;
    double lock_wait_ms = 0.0;
    double prepare_ms = 0.0;
    double inference_ms = 0.0;
    double postprocess_ms = 0.0;
    double total_ms = 0.0;
};

struct BgeEmbeddingResult {
    bool ok = false;

    std::vector<float> embedding;
    std::string error;
    BgeEmbeddingTiming timing;
};

// 将查询文本编码为BGE向量。
class BgeEmbedder final {
public:
    explicit BgeEmbedder(BgeEmbedderConfig config);

    ~BgeEmbedder();

    BgeEmbedder(const BgeEmbedder&) = delete;
    BgeEmbedder& operator=(const BgeEmbedder&) = delete;

    bool load();

    BgeEmbeddingResult encodeQuery(const std::string& query) const;

    // Explicit profiling path; normal queries do not read the stage clocks.
    BgeEmbeddingResult encodeQueryProfiled(const std::string& query) const;

    bool isLoaded() const noexcept;
    int dimension() const noexcept;

    const std::string& lastError() const noexcept;

private:
    BgeEmbeddingResult encodeQueryImpl(const std::string& query, bool profile) const;

    struct Impl;

    BgeEmbedderConfig config_;
    std::unique_ptr<Impl> impl_;

    bool loaded_ = false;
    std::string last_error_;
};
