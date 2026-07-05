#pragma once

#include <string>

enum class QueryType {
    VehicleManual,
    chat,
    Unknown
};

class QueryRouter {
public:
    QueryType classify(const std::string& query) const;
    std::string typeToString(QueryType) const;
};