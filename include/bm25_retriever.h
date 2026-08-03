#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "chinese_text_tokenizer.h"
#include "retriever.h"

/*
 * BM25参数。
 *
 * k1:
 *   控制词频增长速度。
 *   词语重复出现时，分数会增长，但不会无限线性增长。
 *
 * b:
 *   控制文档长度归一化。
 *   0表示不考虑文档长度，1表示完全归一化。
 *
 * minimum_matched_terms:
 *   至少匹配多少个不同查询token才返回结果。
 *   用于降低只命中“的”等单个常见汉字造成的误召回。
 */
struct Bm25RetrieverConfig {
    float k1 = 1.2F;
    float b = 0.75F;

    std::size_t minimum_matched_terms = 2;
};

class Bm25Retriever final : public Retriever {
public:
    explicit Bm25Retriever(
        std::string knowledge_path,
        Bm25RetrieverConfig config = {}
    );

    // 加载json并建立BM25倒排索引
    bool loadKnowledgeBase();

    std::vector<RetrievalResult> searchTopK(
        const std::string& query,
        int top_k
    ) const override;

    // 测试使用
    std::size_t documentCount() const noexcept;
    std::size_t vocabularySize() const noexcept;
    float averageDocumentLength() const noexcept;

private:
    /*
     * 倒排表中的一个节点表示：
     * 某个token在第document_index篇文档中
     * 出现了term_frequency次。
     */
    struct Posting {
        std::size_t document_index = 0;
        std::size_t term_frequency = 0;
    };

    using PostingList = std::vector<Posting>;

    std::string knowledge_path_;
    Bm25RetrieverConfig config_;

    ChineseTextTokenizer tokenizer_;

    std::vector<DocumentChunk> documents_;

    std::vector<std::size_t> document_lengths_; // 每篇文档经过tokenizer之后的token总数。

    /*
     * token -> 包含该token的文档及词频
     *
     * 例如：
     * "空调" -> [
     *     {document_index=0, term_frequency=3}
     * ]
     */
    std::unordered_map<std::string, PostingList> inverted_index_;

    float average_document_length_ = 0.0F;

    void buildIndex();

    float inverseDocumentFrequency(std::size_t document_frequency) const;
};
