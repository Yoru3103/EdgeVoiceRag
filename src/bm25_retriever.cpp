#include "bm25_retriever.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "knowledge_base_loader.h"

Bm25Retriever::Bm25Retriever(
    std::string knowledge_path,
        Bm25RetrieverConfig config
)
    : knowledge_path_(std::move(knowledge_path))
    , config_(config) {
    if (!std::isfinite(config_.k1) || config_.k1 <= 0.0F) {
        throw std::invalid_argument("BM25 k1 must be finite and greater than zero");
    }

    if (
        !std::isfinite(config_.b) ||
        config_.b < 0.0F ||
        config_.b > 1.0F
    ) {
        throw std::invalid_argument("BM25 b must be between zero and one");
    }

    if (config_.minimum_matched_terms == 0) {
        throw std::invalid_argument("BM25 minimum_matched_terms must be positive");
    }
}

bool Bm25Retriever::loadKnowledgeBase() {
    KnowledgeBaseLoadResult load_result = loadDocumentChunks(knowledge_path_);

    if (!load_result.ok) {
        return false;
    }

    // BM25额外要求每篇文档能产生token
    for (const DocumentChunk& document : load_result.documents) {
        if (tokenizer_.tokenize(document.text).empty()) {
            return false;
        }
    }

    documents_ = std::move(load_result.documents);

    buildIndex();

    return !documents_.empty() && !inverted_index_.empty() && average_document_length_ > 0.0F;
}

void Bm25Retriever::buildIndex() {
    inverted_index_.clear();
    document_lengths_.clear();
    average_document_length_ = 0.0F;

    document_lengths_.reserve(documents_.size());

    std::size_t total_document_length = 0;

    for (
        std::size_t document_index = 0;
        document_index < documents_.size();
        document_index++
    ) {
        const std::vector<std::string> tokens = tokenizer_.tokenize(documents_[document_index].text);

        document_lengths_.push_back(tokens.size());

        total_document_length += tokens.size();

        // 先统计当前文档内部每个token的词频
        std::unordered_map<std::string, std::size_t> term_frequencies;

        for (const std::string& token : tokens) {
            term_frequencies[token]++;
        }

        // 把当前文档信息写入全局倒排索引
        for (const auto& [term, frequency] : term_frequencies) {
            Posting posting;
            posting.document_index = document_index;
            posting.term_frequency = frequency;

            inverted_index_[term].push_back(posting);
        }
    }

    if (!documents_.empty()) {
        average_document_length_ = static_cast<float>(total_document_length) / static_cast<float>(documents_.size());
    }
}

float Bm25Retriever::inverseDocumentFrequency(std::size_t document_frequency) const {
    /*
     * 使用Robertson/Sparck Jones形式并加1：
     *
     * IDF = log(
     *     1 +
     *     (N - df + 0.5) / (df + 0.5)
     * )
     *
     * N:
     *   文档总数
     *
     * df:
     *   包含该token的文档数量
     *
     * token越少见，IDF越大。
     */
    const float document_count = static_cast<float>(documents_.size());
    const float frequency = static_cast<float>(document_frequency);

    return std::log(
        1.0F + (document_count - frequency + 0.5) / (frequency + 0.5F)
    );
}

std::vector<RetrievalResult> Bm25Retriever::searchTopK(
    const std::string& query,
    int top_k
) const {
    std::vector<RetrievalResult> results;

    if (
        query.empty() ||
        top_k <= 0 ||
        documents_.empty() ||
        inverted_index_.empty() ||
        average_document_length_ <= 0.0F
    ) {
        return results;
    }

    const std::vector<std::string> query_tokens = tokenizer_.tokenize(query);
    if (query_tokens.empty()) {
        return results;
    }

    /*
     * BM25的这个版本只关心查询token是否出现，
     * 不让用户重复说同一个词无限增加分数。
     *
     * 但文档中的重复词仍然会计算TF。
     */
    const std::unordered_set<std::string> unique_query_terms(
        query_tokens.begin(),
        query_tokens.end()
    );

    std::vector<float> scores(documents_.size(), 0.0F);

    // 记录每篇文档命中了多少个查询token
    std::vector<std::size_t> matched_term_counts(documents_.size(), 0);

    for (const std::string& term : unique_query_terms) {
        const auto index_iterator = inverted_index_.find(term);

        if (index_iterator == inverted_index_.end()) {
            continue;
        }

        const PostingList& postings = index_iterator->second;

        const float idf = inverseDocumentFrequency(postings.size());

        for (const Posting& posting : postings) {
            const std::size_t document_index = posting.document_index;

            const float term_frequency = static_cast<float>(posting.term_frequency);

            const float document_length = static_cast<float>(document_lengths_[document_index]);

            /*
             * BM25文档长度归一化部分：
             *
             * 1 - b + b * 文档长度 / 平均文档长度
             */
            const float length_normalization = 
                1.0F - config_.b + config_.b * document_length / average_document_length_;

            const float denominator = term_frequency + config_.k1 * length_normalization;

            const float term_score = 
                idf * (term_frequency * (config_.k1 + 1.0F)) / denominator;

            scores[document_index] += term_score;

            matched_term_counts[document_index]++;
        }
    }

    // 可能只命中一个token
    const std::size_t required_matches = std::min(
        config_.minimum_matched_terms, unique_query_terms.size()
    );

    for (
        std::size_t document_index = 0;
        document_index < documents_.size();
        document_index++
    ) {
        if (
            scores[document_index] <= 0.0F ||
            matched_term_counts[document_index] < required_matches
        ) {
            continue;
        }

        RetrievalResult result;
        result.chunk = documents_[document_index];
        result.sparse_score = scores[document_index];
        result.dense_score = 0.0F;

        /*
         * 当前还没有语义向量检索，
         * 因此最终分数等于BM25分数。
         */
        result.final_score = result.sparse_score;

        results.push_back(std::move(result));
    }

    std::sort(
        results.begin(),
        results.end(),
        [](
            const RetrievalResult& left,
            const RetrievalResult& right
        ) {
            if (
                left.final_score !=
                right.final_score
            ) {
                return
                    left.final_score >
                    right.final_score;
            }

            return
                left.chunk.chunk_id <
                right.chunk.chunk_id;
        }
    );

    const std::size_t result_limit = static_cast<std::size_t>(top_k);

    if (results.size() > result_limit) {
        results.resize(result_limit);
    }

    return results;
}

std::size_t Bm25Retriever::documentCount() const noexcept {
    return documents_.size();
}

std::size_t Bm25Retriever::vocabularySize() const noexcept {
    return inverted_index_.size();
}

float Bm25Retriever::averageDocumentLength() const noexcept {
    return average_document_length_;
}
