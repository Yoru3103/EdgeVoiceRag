#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bm25_retriever.h"
#include "policy_routing_answer_backend.h"

namespace {

int failed_count = 0;

void expectTrue(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    failed_count++;
}

RetrievalResult makeResult(
    std::string text,
    float sparse_score,
    float dense_score
) {
    RetrievalResult result;
    result.chunk.chunk_id = 1;
    result.chunk.text = std::move(text);
    result.sparse_score = sparse_score;
    result.dense_score = dense_score;
    return result;
}

class RecordingRetriever final : public Retriever {
public:
    mutable int call_count = 0;
    std::vector<RetrievalResult> results;

    std::vector<RetrievalResult> searchTopK(
        const std::string&,
        int top_k
    ) const override {
        call_count++;

        std::vector<RetrievalResult> selected = results;
        if (selected.size() > static_cast<std::size_t>(top_k)) {
            selected.resize(static_cast<std::size_t>(top_k));
        }
        return selected;
    }
};

class RecordingLlm final : public LlmBackend {
public:
    int call_count = 0;
    int cancel_count = 0;
    std::string last_prompt;
    std::string answer = "这是模型生成的回答。";

    std::string name() const override {
        return "recording_llm";
    }

    LlmGenerationResult generate(const std::string& prompt) override {
        call_count++;
        last_prompt = prompt;
        return LlmGenerationResult::success(answer);
    }

    LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) override {
        call_count++;
        last_prompt = prompt;
        callback(answer.substr(0, 9));
        callback(answer.substr(9));
        return LlmGenerationResult::success(answer);
    }

    bool cancel() override {
        cancel_count++;
        return true;
    }
};

void testEmergencyUsesGuardedSafetyResponse() {
    RecordingRetriever retriever;
    retriever.results = {
        makeResult("制动警告灯亮起时应立即检查制动系统。", 12.0F, 0.7F)
    };
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    std::vector<RagStreamEvent> events;
    const RagStreamQueryResult result = backend.query(
        {"policy-1", "制动系统故障，非常危险"},
        [&events](const RagStreamEvent& event) { events.push_back(event); }
    );

    expectTrue(result.ok, "emergency response succeeds");
    expectTrue(result.response_mode == "safety", "emergency uses safety mode");
    expectTrue(result.query_category == "emergency", "report emergency category");
    expectTrue(result.answer.find("行车安全") != std::string::npos, "include safety guardrail");
    expectTrue(result.answer.find("车辆手册相关说明") != std::string::npos, "append manual evidence");
    expectTrue(llm.call_count == 0, "safety mode skips LLM");
    expectTrue(events.size() == 2, "static response emits chunk and finish");
}

void testHighConfidenceFactUsesDirectRag() {
    RecordingRetriever retriever;
    retriever.results = {
        makeResult("蓝牙可在中控屏的连接设置中完成配对。", 9.0F, 0.2F)
    };
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult result = backend.query(
        {"policy-2", "蓝牙怎么连接"}
    );

    expectTrue(result.ok, "direct RAG response succeeds");
    expectTrue(result.response_mode == "direct_rag", "fact uses direct RAG");
    expectTrue(result.answer.find("根据车辆手册") == 0, "direct answer identifies source");
    expectTrue(llm.call_count == 0, "direct RAG skips LLM");
    expectTrue(result.retrieval_result_count == 1, "report retrieval count");
}

void testCommonVehicleFactUsesDirectRag() {
    RecordingRetriever retriever;
    retriever.results = {
        makeResult("空调可通过中控屏的空调按钮开启。", 9.1F, 0.3F)
    };
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult result = backend.query(
        {"policy-common-fact", "空调怎么打开"}
    );

    expectTrue(result.ok, "common vehicle fact succeeds");
    expectTrue(result.query_category == "factual", "single vehicle term is factual");
    expectTrue(result.response_mode == "direct_rag", "common strong fact uses direct RAG");
    expectTrue(llm.call_count == 0, "common direct fact skips LLM");
}

void testNonCriticalFaultDoesNotTriggerSafety() {
    RecordingRetriever retriever;
    retriever.results = {
        makeResult("蓝牙异常时可删除配对记录后重新连接。", 11.0F, 0.6F)
    };
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult result = backend.query(
        {"policy-noncritical", "蓝牙连接故障怎么办"}
    );

    expectTrue(result.ok, "non-critical fault response succeeds");
    expectTrue(result.query_category == "factual", "non-critical fault stays factual");
    expectTrue(result.response_mode == "direct_rag", "non-critical fault avoids safety mode");
}

void testLowerConfidenceFactUsesRagLlm() {
    RecordingRetriever retriever;
    retriever.results = {
        makeResult("蓝牙可在中控屏的连接设置中完成配对。", 6.5F, 0.45F)
    };
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult result = backend.query(
        {"policy-3", "蓝牙怎么连接"}
    );

    expectTrue(result.ok, "RAG LLM response succeeds");
    expectTrue(result.response_mode == "rag_llm", "weaker fact uses RAG LLM");
    expectTrue(llm.call_count == 1, "RAG LLM calls model once");
    expectTrue(llm.last_prompt.find("蓝牙可在") != std::string::npos, "RAG prompt contains evidence");
    expectTrue(result.timing.first_token_observed, "record LLM first token");
}

void testCreativeSkipsRetrieval() {
    RecordingRetriever retriever;
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult result = backend.query(
        {"policy-4", "推荐旅行路线、美食和游玩攻略"}
    );

    expectTrue(result.ok, "LLM-only response succeeds");
    expectTrue(result.response_mode == "llm_only", "creative uses LLM only");
    expectTrue(retriever.call_count == 0, "LLM-only skips retrieval");
    expectTrue(llm.call_count == 1, "LLM-only calls model");
    expectTrue(llm.last_prompt.find("不需要查询车辆手册") != std::string::npos, "use LLM-only prompt");
}

void testUnknownWithoutEvidenceClarifies() {
    RecordingRetriever retriever;
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult result = backend.query(
        {"policy-5", "你好"}
    );

    expectTrue(result.ok, "clarification response succeeds");
    expectTrue(result.response_mode == "clarification", "unknown query clarifies");
    expectTrue(result.answer.find("请说明") != std::string::npos, "ask for concrete context");
    expectTrue(retriever.call_count == 1, "unknown query checks manual evidence");
    expectTrue(llm.call_count == 0, "clarification skips LLM");
}

void testPolicyThresholdValidationAndDenseConfidence() {
    bool rejected = false;
    try {
        ResponsePolicyConfig config;
        config.direct_rag_minimum_dense_similarity = 1.0F;
        ResponsePolicy invalid(config);
        (void)invalid;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expectTrue(rejected, "reject invalid dense threshold");

    ResponsePolicy policy;
    const RetrievalResult dense = makeResult("dense", 0.0F, 0.60F);
    expectTrue(policy.isHighConfidence(dense), "accept high-confidence dense result");
}

void testInvalidRequestAndIdleCancellation() {
    RecordingRetriever retriever;
    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    expectTrue(!backend.query({"", "问题"}).ok, "reject empty request id");
    expectTrue(!backend.query({"request", ""}).ok, "reject empty query");

    const CancellationResult cancellation = backend.cancel("idle");
    expectTrue(cancellation.ok, "idle cancellation is valid");
    expectTrue(!cancellation.cancelled, "idle cancellation has no target");
}

void testCurrentKnowledgeBaseRouting() {
    Bm25Retriever retriever("vector_db/chunks.json");
    expectTrue(retriever.loadKnowledgeBase(), "load current knowledge base");

    RecordingLlm llm;
    PolicyRoutingAnswerBackend backend(retriever, llm);

    const RagStreamQueryResult air_conditioner = backend.query(
        {"policy-real-1", "空调怎么打开"}
    );
    expectTrue(
        air_conditioner.response_mode == "direct_rag",
        "current knowledge routes strong fact directly"
    );

    const RagStreamQueryResult tire_pressure = backend.query(
        {"policy-real-2", "胎压报警怎么办"}
    );
    expectTrue(
        tire_pressure.response_mode == "safety",
        "current knowledge guards tire-pressure alarm"
    );

    const RagStreamQueryResult joke = backend.query(
        {"policy-real-3", "给我讲一个笑话"}
    );
    expectTrue(
        joke.response_mode == "llm_only",
        "current knowledge skips retrieval for creative query"
    );
}

}   // namespace

int main() {
    testEmergencyUsesGuardedSafetyResponse();
    testHighConfidenceFactUsesDirectRag();
    testCommonVehicleFactUsesDirectRag();
    testNonCriticalFaultDoesNotTriggerSafety();
    testLowerConfidenceFactUsesRagLlm();
    testCreativeSkipsRetrieval();
    testUnknownWithoutEvidenceClarifies();
    testPolicyThresholdValidationAndDenseConfidence();
    testInvalidRequestAndIdleCancellation();
    testCurrentKnowledgeBaseRouting();

    if (failed_count == 0) {
        std::cout << "\nAll policy routing answer backend tests passed.\n";
        return 0;
    }

    std::cout << "\nFailed tests: " << failed_count << '\n';
    return 1;
}
