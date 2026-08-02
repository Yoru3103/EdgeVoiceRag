#pragma once

#include <string>
#include <vector>

#include "retriever.h"

// 负责本地关键词检索器
// 修改为继承类的形式，统一接口
class RagEngine final : public Retriever {
public:
    // explicit禁止构造函数或转换函数发生隐式类型转换，避免单参数时编译器将User u = 10转换为User(10)。
    explicit RagEngine(const std::string& knowledge_path);

    // 从chunks.json读取知识库
    bool loadKnowledgeBase();

    // 根据问题检索最相关条目
    std::vector<RetrievalResult> searchTopK(const std::string& query, int top_k) const override;

private:
    struct KeywordRule {
        std::vector<std::string> triggers;
        std::vector<std::string> keywords;
    };

    std::string knowledge_path_;    // 知识库文件路径

    std::vector<DocumentChunk> documents_;    // 按行加载后的知识条目
    std::vector<KeywordRule> keyword_rules_;
    
    // 从问题中提取关键词
    std::vector<std::string> extractKeywords(const std::string& query) const;

    float calculateScore(
        const DocumentChunk& document,
        const std::vector<std::string>& keywords
    ) const;

    static bool containsKeyword(
        const std::string& text,
        const std::string& keyword
    );
};
