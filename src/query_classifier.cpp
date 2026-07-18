#include "query_classifier.h"

#include <algorithm>

namespace {

bool vectorContains(
    const std::vector<std::string>& values,
    const std::string& value
) {
    return std::find(
        values.begin(),
        values.end(),
        value
    ) != values.end();
}

float clampScore(float value) {
    return std::min(1.0F, value);
}

}   // namespace

QueryClassifier::QueryClassifier()
    : keyword_dictionary_{
        {
            "emergency",
            {
                "故障",
                "警告",
                "危险",
                "紧急",
                "异常",
                "失灵",
                "失效",
                "安全气囊",
                "制动故障",
                "发动机故障"
            }
        },
        {
            "technical",
            {
                "发动机",
                "制动",
                "变速箱",
                "电气",
                "空调",
                "转向",
                "悬挂",
                "轮胎",
                "胎压",
                "机油",
                "冷却液",
                "电瓶"
            }
        },
        {
            "maintenance",
            {
                "保养",
                "维修",
                "更换",
                "检查",
                "清洁",
                "调整",
                "润滑",
                "滤清器",
                "火花塞",
                "制动片"
            }
        },
        {
            "feature",
            {
                "自动泊车",
                "车道保持",
                "定速巡航",
                "导航",
                "空调控制",
                "座椅调节",
                "雨刷",
                "灯光",
                "蓝牙"
            }
        },
        {
            "question",
            {
                "什么",
                "怎么",
                "如何",
                "为什么",
                "哪里",
                "何时",
                "多少",
                "能不能",
                "有没有"
            }
        },
        {
            "creative",
            {
                "推荐",
                "建议",
                "规划",
                "设计",
                "故事",
                "笑话",
                "旅行",
                "路线",
                "攻略",
                "美食",
                "翻译"
            }
        }
    } {
}

QueryFeatures QueryClassifier::analyze(const std::string& query) const {
    QueryFeatures features;

    features.query_length = query.size();
    features.keywords = extractKeywords(query);

    features.urgency_score = calculateUrgency(features.keywords);

    features.factual_score = calculateFactual(features.keywords);

    features.creative_score = calculateCreative(features.keywords);

    features.complexity_score = (
        calculateComplexity(
            query,
            features.keywords
        )
    );

    features.contains_question_words = (
        containsAny(
            query,
            keyword_dictionary_.at("question")
        )
    );

    features.contains_emergency_words = (
        containsAny(
            query,
            keyword_dictionary_.at(
                "emergency"
            )
        )
    );

    features.contains_technical_words = (
        containsAny(
            query,
            keyword_dictionary_.at(
                "technical"
            )
        )
    );

    return features;
}

QueryClassification QueryClassifier::classify(const std::string& query) const {
    const QueryFeatures features = analyze(query);

    QueryClassification result;

    if (
        features.contains_emergency_words
        || features.urgency_score >= 0.7F
    ) {
        result.category = QueryCategory::Emergency;
        result.confidence = std::max(
            features.urgency_score,
            0.8F
        );
        result.reasoning = (
            "query contains emergency keywords"
        );
        result.requires_immediate_response = true;
        return result;
    }

    if (features.factual_score >= 0.5F) {
        result.category = QueryCategory::Factual;
        result.confidence = (
            features.factual_score
        );
        result.reasoning = (
            "query matches vehicle facts or "
            "maintenance knowledge"
        );
        return result;
    }

    if (features.creative_score >= 0.6F) {
        result.category = QueryCategory::Creative;
        result.confidence = (
            features.creative_score
        );
        result.reasoning = (
            "query is open-ended or creative"
        );
        return result;
    }

    if (features.complexity_score >= 0.6F) {
        result.category = QueryCategory::Complex;
        result.confidence = (
            features.complexity_score
        );
        result.reasoning = (
            "query requires multi-step reasoning"
        );
        return result;
    }

    result.category = QueryCategory::Unknown;
    result.confidence = 0.0F;
    result.reasoning = (
        "no response strategy matched"
    );

    return result;
}

std::string QueryClassifier::categoryToString(QueryCategory category) const {
    switch (category) {
        case QueryCategory::Emergency:
            return "emergency";

        case QueryCategory::Factual:
            return "factual";

        case QueryCategory::Complex:
            return "complex";

        case QueryCategory::Creative:
            return "creative";

        case QueryCategory::Unknown:
        default:
            return "unknown";
    }
}

bool QueryClassifier::containsAny(
    const std::string& query,
    const std::vector<std::string>& words
) const {
    for (const auto& word: words) {
        if (query.find(word) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::vector<std::string> QueryClassifier::extractKeywords(const std::string& query) const {
    std::vector<std::string> result;

    for (const auto& category: keyword_dictionary_) {
        for (const auto& word: category.second) {
            if (query.find(word) != std::string::npos && !vectorContains(result, word)) {
                result.push_back(word);
            }
        }
    }

    return result;
}

float QueryClassifier::calculateUrgency(const std::vector<std::string>& keywords) const {
    int count = 0;

    const auto& emergency_words = keyword_dictionary_.at("emergency");

    for (const auto& keyword: keywords) {
        if (vectorContains(emergency_words, keyword)) {
            count++;
        }
    }

    return clampScore(static_cast<float>(count) * 0.3F);
}

float QueryClassifier::calculateFactual(const std::vector<std::string>& keywords) const {
    float score = 0.0f;

    const auto& technical_words = keyword_dictionary_.at("technical");
    const auto& maintenance_words = keyword_dictionary_.at("maintenance");
    const auto& feature_words = keyword_dictionary_.at("feature");

    for (const auto& keyword: keywords) {
        if (vectorContains(technical_words, keyword)) {
            score += 0.4f;
        }

        if (vectorContains(maintenance_words, keyword)) {
            score += 0.4f;
        }

        if (vectorContains(feature_words, keyword)) {
            score += 0.5f;
        }
    }

    return clampScore(score);
}

float QueryClassifier::calculateCreative(const std::vector<std::string>& keywords) const {
    float score = 0.0f;

    const auto& creative_words = keyword_dictionary_.at("creative");

    for (const auto& keyword: keywords) {
        if (vectorContains(creative_words, keyword)) {
            score += 0.3f;
        }
    }

    return clampScore(score);
}

float QueryClassifier::calculateComplexity(
    const std::string& query,
    const std::vector<std::string>& keywords
) const {
    float score = 0.0f;

    score += std::min(1.0f, static_cast<float>(query.size()) / 100.0f) * 0.3f;

    score += std::min(1.0f, static_cast<float>(keywords.size()) / 10.0f) * 0.4f;

    int technical_count = 0;

    const auto& technical_words = keyword_dictionary_.at("technical");

    for (const auto& keyword: keywords) {
        if (vectorContains(technical_words, keyword)) {
            technical_count++;
        }
    }

    score += std::min(1.0f, static_cast<float>(technical_count) / 5.0f) * 0.3f;

    return clampScore(score);
}