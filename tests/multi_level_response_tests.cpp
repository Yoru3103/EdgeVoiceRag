#include <iostream>
#include <string>

#include "mock_response_backend.h"
#include "multi_level_response_system.h"

namespace {


int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS]" << name << '\n';
        return;
    }

    std::cout << "[FAIL]" << name << '\n';

    failed_count++;
}

void testEmergencyUsesRagOnly() {
    MockResponseBackend backend;

    MultiLevelResponseSystem system(backend);

    const auto result = system.process("制动系统故障，非常危险");

    expectTrue(backend.rag_call_count == 1, "emergency calls RAG");
    
    expectTrue(result.ok, "emergency response succeeds");

    expectTrue(result.category == QueryCategory::Emergency, "emergency category");

    expectTrue(result.mode == ResponseMode::RagOnly, "emergency uses RAG only");

    expectTrue(backend.llm_call_count == 0, "emergency skips LLM");
}

void testFactualUsesRagOnly() {
    MockResponseBackend backend;

    MultiLevelResponseSystem system(
        backend
    );

    const auto result = system.process(
        "蓝牙应该怎么连接"
    );

    expectTrue(
        result.ok,
        "factual response succeeds"
    );

    expectTrue(
        result.category
            == QueryCategory::Factual,
        "factual category"
    );

    expectTrue(
        result.mode
            == ResponseMode::RagOnly,
        "factual uses RAG only"
    );
}

void testCreativeUsesLlmOnly() {
    MockResponseBackend backend;

    MultiLevelResponseSystem system(
        backend
    );

    const auto result = system.process(
        "推荐旅行路线、美食和游玩攻略"
    );

    expectTrue(
        result.ok,
        "creative response succeeds"
    );

    expectTrue(
        result.category
            == QueryCategory::Creative,
        "creative category"
    );

    expectTrue(
        result.mode
            == ResponseMode::LlmOnly,
        "creative uses LLM only"
    );

    expectTrue(
        backend.rag_call_count == 0,
        "creative skips RAG"
    );

    expectTrue(
        backend.llm_call_count == 1,
        "creative calls LLM"
    );
}

void testUnknownUsesHybrid() {
    MockResponseBackend backend;

    MultiLevelResponseSystem system(
        backend
    );

    const auto result = system.process(
        "你好"
    );

    expectTrue(
        result.ok,
        "unknown response succeeds"
    );

    expectTrue(
        result.mode
            == ResponseMode::Hybrid,
        "unknown uses hybrid"
    );

    expectTrue(
        backend.rag_call_count == 1,
        "hybrid calls RAG"
    );

    expectTrue(
        backend.llm_call_count == 1,
        "hybrid calls LLM"
    );

    expectTrue(
        backend.last_llm_prompt.find(
            "<rag>"
        ) != std::string::npos,
        "hybrid prompt contains RAG context"
    );
}

void testHybridFallsBackToLlm() {
    MockResponseBackend backend;

    backend.rag_result = (
        BackendResult::failure(
            "no RAG result"
        )
    );

    MultiLevelResponseSystem system(
        backend
    );

    const auto result = system.process(
        "你好"
    );

    expectTrue(
        result.ok,
        "hybrid falls back to LLM"
    );

    expectTrue(
        backend.llm_call_count == 1,
        "fallback calls LLM"
    );

    expectTrue(
        backend.last_llm_prompt
            == "你好",
        "fallback excludes failed RAG"
    );
}

void testCacheAvoidsRepeatedCalls() {
    MockResponseBackend backend;

    MultiLevelResponseSystem system(
        backend
    );

    const auto first = system.process(
        "蓝牙应该怎么连接"
    );

    const auto second = system.process(
        "蓝牙应该怎么连接"
    );

    expectTrue(
        first.ok && second.ok,
        "cached responses succeed"
    );

    expectTrue(
        !first.from_cache,
        "first response is not cached"
    );

    expectTrue(
        second.from_cache,
        "second response is cached"
    );

    expectTrue(
        backend.rag_call_count == 1,
        "cache prevents repeated RAG call"
    );

    expectTrue(
        system.cacheSize() == 1,
        "cache contains one result"
    );
}

void testFailureIsNotCached() {
    MockResponseBackend backend;

    backend.rag_result = (
        BackendResult::failure(
            "RAG timeout"
        )
    );

    MultiLevelResponseSystem system(
        backend
    );

    const auto first = system.process(
        "蓝牙应该怎么连接"
    );

    const auto second = system.process(
        "蓝牙应该怎么连接"
    );

    expectTrue(
        !first.ok && !second.ok,
        "failed responses remain failures"
    );

    expectTrue(
        backend.rag_call_count == 2,
        "failed response is not cached"
    );

    expectTrue(
        system.cacheSize() == 0,
        "failure cache remains empty"
    );
}

void testEmptyQueryRejected() {
    MockResponseBackend backend;

    MultiLevelResponseSystem system(
        backend
    );

    const auto result = system.process("");

    expectTrue(
        !result.ok,
        "empty query is rejected"
    );

    expectTrue(
        result.error == "query is empty",
        "empty query returns reason"
    );

    expectTrue(
        backend.rag_call_count == 0,
        "empty query skips RAG"
    );

    expectTrue(
        backend.llm_call_count == 0,
        "empty query skips LLM"
    );
}

}  // namespace

int main() {
    testEmergencyUsesRagOnly();
    testFactualUsesRagOnly();
    testCreativeUsesLlmOnly();
    testUnknownUsesHybrid();
    testHybridFallsBackToLlm();
    testCacheAvoidsRepeatedCalls();
    testFailureIsNotCached();
    testEmptyQueryRejected();

    if (failed_count == 0) {
        std::cout
            << "\nAll multi-level response "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
