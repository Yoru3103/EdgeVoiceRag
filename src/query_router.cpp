#include "query_router.h"

QueryType QueryRouter::classify(const std::string& query) const {
    if (query.find("空调") != std::string::npos ||
        query.find("蓝牙") != std::string::npos ||
        query.find("胎压") != std::string::npos) {
        return QueryType::VehicleManual;
    }

    if (query.find("你好") != std::string::npos ||
        query.find("天气") != std::string::npos) {
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