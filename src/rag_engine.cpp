#include "rag_engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>

RagEngine::RagEngine(const std::string& knowledge_path)
    : knowledge_path_(knowledge_path) {

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

    if (query.find("空调") != std::string::npos) {
        keywords.push_back("空调");
    }

    if (query.find("温度") != std::string::npos ||
        query.find("调温") != std::string::npos) {
        keywords.push_back("温度");
    }

    if (query.find("蓝牙") != std::string::npos) {
        keywords.push_back("蓝牙");
    }

    if (query.find("胎压") != std::string::npos) {
        keywords.push_back("胎压");
    }

    if (query.find("座椅") != std::string::npos ||
        query.find("加热") != std::string::npos) {
        keywords.push_back("座椅");
        keywords.push_back("加热");
    }

    if (query.find("导航") != std::string::npos) {
        keywords.push_back("导航");
    }

    if (query.find("雨刮") != std::string::npos ||
        query.find("雨刷") != std::string::npos) {
        keywords.push_back("雨刮");
    }

    if (query.find("车窗") != std::string::npos) {
        keywords.push_back("车窗");
    }

    if (query.find("后备箱") != std::string::npos ||
        query.find("尾门") != std::string::npos) {
        keywords.push_back("后备箱");
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