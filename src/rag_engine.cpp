#include "rag_engine.h"

#include <fstream>
#include <sstream>

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

std::string RagEngine::search(const std::string& query) const {
    std::vector<std::string> keywords = extractKeywords(query);

    for (const auto& doc : documents_) {
        if (containsAnyKeyword(doc, keywords)) {
            return doc;
        }
    }

    return "知识库中没有找到相关车辆手册内容。";
}

std::vector<std::string> RagEngine::extractKeywords(const std::string& query) const {
    std::vector<std::string> keywords;

    if (query.find("空调") != std::string::npos) {
        keywords.push_back("空调");
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

bool RagEngine::containsAnyKeyword(const std::string& text,
                            const std::vector<std::string>& keywords) const {
    for (const auto& keyword : keywords) {
        if (text.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}