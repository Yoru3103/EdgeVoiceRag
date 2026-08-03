#include <iostream>
#include <string>
#include <vector>

#include "query_router.h"
#include "bm25_retriever.h"
#include "rag_response_parser.h"
#include "query_classifier.h"

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
    Bm25Retriever bm25_retriever("vector_db/chunks.json");

    expectTrue(
        bm25_retriever.loadKnowledgeBase(),
        "Bm25Retriever: load knowledge base"
    );

    {
        std::vector<RetrievalResult> results = bm25_retriever.searchTopK("空调怎么打开", 3);

        expectTrue(
            !results.empty(),
            "Bm25Retriever: air conditioner query should return results"
        );
    }

    {
        std::vector<RetrievalResult> results = bm25_retriever.searchTopK("蓝牙怎么连接", 3);

        expectTrue(
            !results.empty(),
            "Bm25Retriever: bluetooth query should return results"
        );

        if (!results.empty()) {
            expectTrue(
                results[0].chunk.title.find("蓝牙连接") != std::string::npos,
                "Bm25Retriever: bluetooth query should hit bluetooth document"
            );
            expectTrue(
                results[0].chunk.text.find("蓝牙连接") != std::string::npos,
                "Bm25Retriever: bluetooth result should contain document text"
            );
            expectTrue(
                results[0].sparse_score > 0.0F,
                "Bm25Retriever: sparse score should be positive"
            );
            expectTrue(
                results[0].dense_score == 0.0F,
                "Bm25Retriever: dense score should not be used yet"
            );
            expectTrue(
                results[0].final_score == results[0].sparse_score,
                "Bm25Retriever: final score should equal sparse score"
            );
        }
    }

    {
        std::vector<RetrievalResult> results = bm25_retriever.searchTopK("完全无关的问题", 3);

        expectTrue(
            results.empty(),
            "Bm25Retriever: unrelated query should return empty results"
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

    {
        const std::vector<RetrievalResult> results = bm25_retriever.searchTopK("", 3);

        expectTrue(
            results.empty(),
            "Bm25Retriever: empty query should return no results"
        );
    }

    {
        const std::vector<RetrievalResult> results = bm25_retriever.searchTopK("空调怎么打开", 0);

        expectTrue(
            results.empty(),
            "Bm25Retriever: non-positive top_k should return no results"
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

static void testQueryClassifier() {
    QueryClassifier classifier;

    {
        const auto result = classifier.classify(
            "制动系统故障，非常危险"
        );

        expectTrue(
            result.category
                == QueryCategory::Emergency,
            "QueryClassifier: emergency query"
        );

        expectTrue(
            result.requires_immediate_response,
            "QueryClassifier: emergency query "
            "requires immediate response"
        );
    }

    {
        const auto result = classifier.classify(
            "蓝牙应该怎么连接"
        );

        expectTrue(
            result.category
                == QueryCategory::Factual,
            "QueryClassifier: factual query"
        );
    }

    {
        const auto result = classifier.classify(
            "推荐一个旅行路线和美食攻略"
        );

        expectTrue(
            result.category
                == QueryCategory::Creative,
            "QueryClassifier: creative query"
        );
    }

    {
        const auto result = classifier.classify(
            "你好"
        );

        expectTrue(
            result.category
                == QueryCategory::Unknown,
            "QueryClassifier: unknown query"
        );
    }
}

int main() {
    std::cout << "Running unit tests..." << std::endl;

    testQueryRouter();
    testRagEngine();
    testRagResponseParser();
    testQueryClassifier();

    if (g_failed_count == 0) {
        std::cout << "\nAll unit tests passed." << std::endl;
        return 0;
    }

    std::cout << "\nUnit tests failed: " << g_failed_count << std::endl;
    return 1;
}