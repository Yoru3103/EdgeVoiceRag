#pragma once

#include <string>
#include <unordered_map>
#include <vector>

enum class QueryCategory {
    Emergency,
    Factual,
    Complex,
    Creative,
    Unknown
};

struct QueryFeatures {
    std::vector<std::string> keywords;

    float urgency_score = 0.0F;
    float complexity_score = 0.0F;
    float factual_score = 0.0F;
    float creative_score = 0.0F;

    std::size_t query_length = 0;

    bool contains_question_words = false;
    bool contains_emergency_words = false;
    bool contains_technical_words = false;
};

struct QueryClassification {
    QueryCategory category = (QueryCategory::Unknown);

    float confidence = 0.0F;

    std::string reasoning;

    bool requires_immediate_response = false;
};

class QueryClassifier {
public:
    QueryClassifier();

    QueryFeatures analyze(const std::string& query) const;

    QueryClassification classify(const std::string& query) const;

    std::string categoryToString(QueryCategory category) const;

private:
    std::unordered_map<
        std::string,
        std::vector<std::string>
    > keyword_dictionary_;

    bool containsAny(
        const std::string& query,
        const std::vector<std::string>& words
    ) const;

    std::vector<std::string> extractKeywords(const std::string& query) const;

    float calculateUrgency(const std::vector<std::string>& keywords) const;

    float calculateFactual(const std::vector<std::string>& keywords) const;

    float calculateCreative(const std::vector<std::string>& keywords) const;

    float calculateComplexity(
        const std::string& query,
        const std::vector<std::string>& keywords
    ) const;
};