#pragma once

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

struct BgeEmbeddingResult {
    bool ok = false;

    std::vector<float> embedding;
    std::string error;
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

    bool isLoaded() const noexcept;
    int dimension() const noexcept;

    const std::string& lastError() const noexcept;

private:
    struct Impl;

    BgeEmbedderConfig config_;
    std::unique_ptr<Impl> impl_;

    bool loaded_ = false;
    std::string last_error_;
};
