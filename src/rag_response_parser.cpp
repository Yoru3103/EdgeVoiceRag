#include "rag_response_parser.h"

#include <nlohmann/json.hpp>

std::string RagResponseParser::extractAnswerOrRaw(const std::string& response) {
    try {
        auto json_response = nlohmann::json::parse(response);

        if (json_response.contains("generated_answer") && 
            json_response["generated_answer"].is_string()) {
            return json_response["generated_answer"].get<std::string>();
        }

        if (json_response.contains("error") && 
            json_response["error"].is_string()) {
            return "[ERROR] " + json_response["error"].get<std::string>();
        }

        if (json_response.contains("answer") &&
            json_response["answer"].is_string()) {
            return json_response["answer"].get<std::string>();
        }

        return response;
    } catch (const std::exception&) {
        return response;
    }
}