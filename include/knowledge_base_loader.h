#pragma once

#include <string>
#include <vector>

#include "retrieval_types.h"

struct KnowledgeBaseLoadResult {
    bool ok = false;

    std::vector<DocumentChunk> documents;
    std::string error;
};

/*
 * 加载并验证chunks.json。
 *
 * BM25、DenseRetriever以及后续HybridRetriever
 * 使用完全相同的文档结构和顺序。
 */
KnowledgeBaseLoadResult loadDocumentChunks(const std::string& path);