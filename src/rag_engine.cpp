#include "rag_engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>

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
    std::ifstream file(knowledge_path_);

    if (!file.is_open()) {
        return false;
    }

    documents_.clear();

    std::string line;
    while (getline(file, line)) {
        if (!line.empty()) {
            documents_.push_back(line);
        }
    }

    return !documents_.empty();
}

std::vector<SearchResult> RagEngine::searchTopK(const std::string& query, int top_k) const {
    std::vector<std::string> keywords = extractKeywords(query);
    std::vector<SearchResult> results;

    if (keywords.empty() || top_k <= 0) {
        return results;
    }

    for (const auto& doc : documents_) {
        int score = calculateScore(doc, keywords);

        if (score > 0) {
            results.push_back(SearchResult{doc, score});
        }
    }

    std::sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
                                                return a.score > b.score;
    });

    if (static_cast<int>(results.size()) > top_k) {
        results.resize(top_k);
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

        if (matched) {
            for (const auto& keyword : rule.keywords) {
                if (std::find(keywords.begin(), keywords.end(), keyword) == keywords.end()) {
                    keywords.push_back(keyword);
                }
            }
        }
    }

    return keywords;
}

int RagEngine::calculateScore(const std::string& document,
                            const std::vector<std::string>& keywords) const {
    int score = 0;
    for (const auto& keyword : keywords) {
        if (document.find(keyword) != std::string::npos) {
            score += 1;
        }
    }

    return score;
}

bool RagEngine::containsKeyword(const std::string& text,
                            const std::string& keyword) const {
    return text.find(keyword) != std::string::npos;                                
}