#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

    // 给LLM的原始车辆手册文本
    std::string text;

    // 用户可能使用的同义表达，不属于车辆手册事实，只参与检索
    std::vector<std::string> aliases;

    // BM25和Dense使用text + aliases构建索引
    std::string retrieval_text;
};

// 检索结果
struct RetrievalResult {
    DocumentChunk chunk;

    float sparse_score = 0.0F;  // 稀疏分数：关键词、TF-IDF 或 BM25 的分数。
    float dense_score = 0.0F;   // 密集分数：BGE 等语义向量模型的余弦相似度。
    float final_score = 0.0F;
};
