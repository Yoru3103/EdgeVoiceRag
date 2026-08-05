#pragma once

#include <cstddef>
#include <string>
#include <vector>

/*
 * DenseVectorIndex只负责向量存储和搜索。
 *
 * 它不知道：
 * - 查询文本是什么；
 * - tokenizer如何工作；
 * - ONNX模型如何推理；
 * - DocumentChunk的具体内容。
 *
 * 它只接收一个浮点向量，并返回最相似的chunk_id。
 */
struct DenseVectorSearchHit {
    std::size_t row = 0;
    int chunk_id = -1;
    float score = 0.0F;
};

class DenseVectorIndex final {
public:
    DenseVectorIndex(
        std::string metadata_path,
        std::string embeddings_path
    );

    /*
     * 加载元数据与二进制向量。
     *
     * 成功返回true。
     * 失败返回false，并可通过lastError()查看原因。
     */
    bool load();

    /*
     * 使用余弦相似度返回Top-K。
     *
     * 文档向量已经归一化；
     * 查询向量即使未归一化，也可以正常搜索。
     */
    std::vector<DenseVectorSearchHit> searchTopK(
        const std::vector<float>& query_embedding,
        int top_k
    ) const;

    bool isLoaded() const noexcept;

    std::size_t rowCount() const noexcept;
    std::size_t dimension() const noexcept;
    int maxLength() const noexcept;

    const std::vector<int>& chunkIds() const noexcept;

    const std::string& modelId() const noexcept;
    const std::string& pooling() const noexcept;
    const std::string& queryInstruction() const noexcept;
    const std::string& lastError() const noexcept;
    const std::string& chunksFingerprint() const noexcept;

private:
    std::string metadata_path_;
    std::string embeddings_path_;

    bool loaded_ = false;

    std::size_t rows_ = 0;
    std::size_t dimension_ = 0;
    int max_length_ = 0;

    std::string model_id_;
    std::string pooling_;
    std::string query_instruction_;
    std::string chunks_fingerprint_;

    std::vector<int> chunk_ids_;

    /*
     * 所有向量连续存储：
     *
     * row 0: [0, dimension)
     * row 1: [dimension, 2 * dimension)
     */
    std::vector<float> embeddings_;

    std::string last_error_;

    void resetData();

    bool fail(
        const std::string& message
    );
};
