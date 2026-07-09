#include <iostream>
#include <string>
#include <vector>

#include "query_router.h"
#include "rag_engine.h"
#include "rag_response_parser.h"

static int g_failed_count = 0;

static void expectTrue(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "[PASS] " << test_name << std::endl;
    } else {
        std::cout << "[FAIL] " << test_name << std::endl;
        g_failed_count++;
    }
}

static void testQueryRouter() {
    QueryRouter router;

    expectTrue(
        router.classify("空调怎么打开") == QueryType::VehicleManual,
        "QueryRouter: air conditioner should be vehicle_manual"
    );

    expectTrue(
        router.classify("蓝牙怎么连接") == QueryType::VehicleManual,
        "QueryRouter: bluetooth should be vehicle_manual"
    );

    expectTrue(
        router.classify("胎压报警怎么办") == QueryType::VehicleManual,
        "QueryRouter: tire pressure should be vehicle_manual"
    );

    expectTrue(
        router.classify("你好") == QueryType::chat,
        "QueryRouter: hello should be chat"
    );

    expectTrue(
        router.classify("今天吃什么") == QueryType::Unknown,
        "QueryRouter: unrelated query should be unknown"
    );
}

static void testRagEngine() {
    RagEngine rag_engine("docs/vehicle_manual.txt");

    expectTrue(
        rag_engine.loadKnowledgeBase(),
        "RagEngine: load knowledge base"
    );

    {
        std::vector<SearchResult> results = rag_engine.searchTopK("空调怎么打开", 3);

        expectTrue(
            !results.empty(),
            "RagEngine: air conditioner query should return results"
        );
    }

    {
        std::vector<SearchResult> results = rag_engine.searchTopK("蓝牙怎么连接", 3);

        expectTrue(
            !results.empty(),
            "RagEngine: bluetooth query should return results"
        );

        if (!results.empty()) {
            expectTrue(
                results[0].document.find("蓝牙连接") != std::string::npos,
                "RagEngine: bluetooth query should hit bluetooth document"
            );
        }
    }

    {
        std::vector<SearchResult> results = rag_engine.searchTopK("完全无关的问题", 3);

        expectTrue(
            results.empty(),
            "RagEngine: unrelated query should return empty results"
        );
    }

    {
        std::string response = R"({"ok":true,"answer":"旧 answer","generated_answer":"LLM 生成 answer"})";
        std::string answer = RagResponseParser::extractAnswerOrRaw(response);

        expectTrue(
        answer == "LLM 生成 answer",
        "RagResponseParser: should prefer generated_answer over answer"
    );
    }
}

static void testRagResponseParser() {
    {
        std::string response = R"({"ok":true,"answer":"根据车辆手册：\n1. 空调系统：..."})";
        std::string answer = RagResponseParser::extractAnswerOrRaw(response);

        expectTrue(
            answer.find("空调系统") != std::string::npos,
            "RagResponseParser: should extract answer from JSON response"
        );
    }

    {
        std::string response = "根据车辆手册：普通文本回答";
        std::string answer = RagResponseParser::extractAnswerOrRaw(response);

        expectTrue(
            answer == response,
            "RagResponseParser: should keep non-JSON response unchanged"
        );
    }

    {
        std::string response = R"({"ok":false,"error":"backend timeout"})";
        std::string answer = RagResponseParser::extractAnswerOrRaw(response);

        expectTrue(
            answer.find("[ERROR] backend timeout") != std::string::npos,
            "RagResponseParser: should extract error from JSON response"
        );
    }
}

int main() {
    std::cout << "Running unit tests..." << std::endl;

    testQueryRouter();
    testRagEngine();
    testRagResponseParser();

    if (g_failed_count == 0) {
        std::cout << "\nAll unit tests passed." << std::endl;
        return 0;
    }

    std::cout << "\nUnit tests failed: " << g_failed_count << std::endl;
    return 1;
}