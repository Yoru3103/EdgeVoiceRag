#pragma once

#include <string>
#include <vector>

enum class QueryType {
    VehicleManual,
    chat,
    Unknown
};

class QueryRouter {
public:
    QueryRouter();

    QueryType classify(const std::string& query) const;
    std::string typeToString(QueryType type) const;

private:
    std::vector<std::string> vehicle_keywords_;
    std::vector<std::string> chat_key_words_;

    bool containsAnyKeyword(const std::string& query,
                            const std::vector<std::string>& keywords) const;
};