#pragma once

#include <string>
#include <vector>

struct SearchResult {
    std::string document;
    int score;
};

class RagEngine {
public:
    // explicit禁止构造函数或转换函数发生隐式类型转换，避免单参数时编译器将User u = 10转换为User(10)。
    explicit RagEngine(const std::string& knowledge_path);

    // 从文件读取知识库
    bool loadKnowledgeBase();

    // 根据问题检索最相关条目
    std::vector<SearchResult> searchTopK(const std::string& query, int top_k) const;

private:
    std::string knowledge_path_;    // 知识库文件路径
    std::vector<std::string> documents_;    // 按行加载后的知识条目
    
    // 从问题中提取关键词
    std::vector<std::string> extractKeywords(const std::string& query) const;

    int calculateScore(const std::string& document,
                            const std::vector<std::string>& keywords) const;
};