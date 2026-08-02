#pragma once

#include <cstddef>
#include <string>

/*
 * 知识库中的一个文档块。
 *
 * 后续无论数据来自：
 * - chunks.json
 * - 数据库
 * - 网络
 * - 预生成向量索引
 *
 * 上层都统一使用 DocumentChunk。
 */
struct DocumentChunk {
    int chunk_id = -1;

    std::string title;
    std::string content;

    /*
     * 用于检索的完整文本。
     * 一般为：title + ": " + content
     */
    std::string text;
};

// 检索结果
struct RetrievalResult {
    DocumentChunk chunk;

    float sparse_score = 0.0F;  // 稀疏分数：关键词、TF-IDF 或 BM25 的分数。
    float dense_score = 0.0F;   // 密集分数：BGE 等语义向量模型的余弦相似度。
    float final_score = 0.0F;
};
