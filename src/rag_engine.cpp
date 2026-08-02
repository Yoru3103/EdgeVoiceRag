#include "rag_engine.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

using Json = nlohmann::json;

RagEngine::RagEngine(const std::string& knowledge_path)
    : knowledge_path_(knowledge_path)
    , keyword_rules_{
        KeywordRule{{"空调", "冷气", "制冷"}, {"空调"}},
        KeywordRule{{"温度", "调温", "升温", "降温"}, {"温度"}},
        KeywordRule{{"蓝牙"}, {"蓝牙"}},
        KeywordRule{{"连接", "配对"}, {"连接", "配对"}},
        KeywordRule{{"胎压", "轮胎气压"}, {"胎压"}},
        KeywordRule{{"报警", "告警", "提示"}, {"报警"}},
        KeywordRule{{"座椅"}, {"座椅"}},
        KeywordRule{{"加热", "座椅加热"}, {"加热"}},
        KeywordRule{{"导航", "路线", "目的地"}, {"导航"}},
        KeywordRule{{"充电站", "充电桩"}, {"充电站"}},
        KeywordRule{{"雨刮", "雨刷"}, {"雨刮", "雨刷"}},
        KeywordRule{{"车窗", "窗户"}, {"车窗"}},
        KeywordRule{{"后备箱", "尾门"}, {"后备箱", "尾门"}}} {
}

bool RagEngine::loadKnowledgeBase() {
    std::ifstream input(knowledge_path_);

    if (!input.is_open()) {
        return false;
    }

    try {
        Json root;
        input >> root;

        if (!root.is_array()) {
            return false;
        }

        // 先用临时变量保存，防止中途解析失败成员变量只加载一半。
        std::vector<DocumentChunk> loaded_documents;
        std::unordered_set<int> loaded_ids;

        loaded_documents.reserve(root.size());

        for (const Json& item : root) {
            if (!item.is_object()) {
                return false;
            }

            if (
                !item.contains("chunk_id") ||
                !item.contains("title") ||
                !item.contains("content")
            ) {
                return false;
            }

            DocumentChunk chunk;

            chunk.chunk_id = item.at("chunk_id").get<int>();
            chunk.title = item.at("title").get<std::string>();
            chunk.content = item.at("content").get<std::string>();

            // text字段可省略 可根据title与content构造
            chunk.text = item.value(
                "text", chunk.title + ": " + chunk.content
            );

            if (
                chunk.chunk_id < 0 ||
                chunk.title.empty() ||
                chunk.content.empty() ||
                chunk.text.empty()
            ) {
                return false;
            }

            // chunk 后续要匹配向量索引，不允许重复
            const bool inserted = loaded_ids.insert(chunk.chunk_id).second;

            if (!inserted) {
                return false;
            }

            loaded_documents.push_back(std::move(chunk));
        }

        if (loaded_documents.empty()) {
            return false;
        }

        documents_ = std::move(loaded_documents);
        return true;
    } catch (const Json::exception&) {
        return false;
    }
}

std::vector<RetrievalResult> RagEngine::searchTopK(const std::string& query, int top_k) const {
    std::vector<RetrievalResult> results;
    if (query.empty() || top_k <= 0) {
        return results;
    }

    std::vector<std::string> keywords = extractKeywords(query);
    if (keywords.empty() || top_k <= 0) {
        return results;
    }

    for (const auto& doc : documents_) {
        const float score = calculateScore(doc, keywords);

        if (score <= 0.0F) {
            continue;
        }

        RetrievalResult result;
        result.chunk = doc;

        result.sparse_score = score;
        result.dense_score = 0.0F;
        result.final_score = score;
        
        results.push_back(std::move(result));
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const RetrievalResult& left, const RetrievalResult& right) {
            if (left.final_score != right.final_score) {
                return left.final_score > right.final_score;
            }
            
            // 分数相同时按照chunk_id排
            return left.chunk.chunk_id < right.chunk.chunk_id;
        }
    );

    const std::size_t result_limit = static_cast<std::size_t>(top_k);

    if (results.size() > result_limit) {
        results.resize(result_limit);
    }

    return results;
}

std::vector<std::string> RagEngine::extractKeywords(const std::string& query) const {
    std::vector<std::string> keywords;

    for (const auto& rule : keyword_rules_) {
        bool matched = false;

        for (const auto& triggers : rule.triggers) {
            if (containsKeyword(query, triggers)) {
                matched = true;
                break;
            }
        }

        if (!matched) {
            continue;
        }

        for (const auto keyword : rule.keywords) {
            const bool already_exists = std::find(keywords.begin(), keywords.end(), keyword) != keywords.end();

            if (!already_exists) {
                keywords.push_back(keyword);
            }
        }
    }

    return keywords;
}

float RagEngine::calculateScore(
    const DocumentChunk& document,
    const std::vector<std::string>& keywords
) const {
    float score = 0.0F;

    for (const auto& keyword : keywords) {
        if (containsKeyword(document.text, keyword)) {
            score += 1.0F;
        }
    }

    return score;
}

bool RagEngine::containsKeyword(
    const std::string& text,
    const std::string& keyword
) {
    return text.find(keyword) != std::string::npos;                                
}