#include "query_router.h"

QueryRouter::QueryRouter()
    : vehicle_keywords_ {
        "空调",
        "冷气",
        "制冷",
        "蓝牙",
        "胎压",
        "轮胎气压",
        "座椅",
        "加热",
        "导航",
        "路线",
        "目的地",
        "雨刮",
        "雨刷",
        "车窗",
        "窗户",
        "后备箱",
        "尾门",
        "充电站",
        "充电桩",
        "温度",
        "调温",
        "配对",
        "连接"
    },
    chat_key_words_ {
        "你好",
        "天气",
        "你是谁",
        "谢谢"
    } {
}

QueryType QueryRouter::classify(const std::string& query) const {
    if (containsAnyKeyword(query, vehicle_keywords_)) {
        return QueryType::VehicleManual;
    }

    if (containsAnyKeyword(query, chat_key_words_)) {
        return QueryType::chat;
    }

    return QueryType::Unknown;
}

std::string QueryRouter::typeToString(QueryType type) const {
    switch (type) {
        case QueryType::chat:
            return "chat";
        case QueryType::VehicleManual:
            return "VehicleManual";
        case QueryType::Unknown:
        default:
            return "Unkown";
    }
}

bool QueryRouter::containsAnyKeyword(const std::string& query, const std::vector<std::string>& keywords) const {
    for (const auto& keyword : keywords) {
        if (query.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}