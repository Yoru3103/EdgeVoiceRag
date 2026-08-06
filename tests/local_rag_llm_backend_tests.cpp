#include <iostream>
#include <string>
#include <vector>

#include "local_rag_llm_backend.h"
#include "mock_llm_backend.h"
#include "bm25_retriever.h"

namespace {

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

class RecordingLlmBackend final : public LlmBackend {
public:
    mutable std::string last_prompt;

    std::string name() const override {
        return "recording_llm";
    }

    LlmGenerationResult generate(
        const std::string& prompt
    ) override {
        last_prompt = prompt;

        return LlmGenerationResult::success(
            "请停车检查轮胎状态，并及时补充胎压。"
        );
    }

    LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) override {
        last_prompt = prompt;

        if (!callback) {
            return LlmGenerationResult::failure(
                "callback must not be empty"
            );
        }

        callback("请停车检查");
        callback("轮胎状态，");
        callback("并及时补充胎压。");

        return LlmGenerationResult::success(
            "请停车检查轮胎状态，并及时补充胎压。"
        );
    }
};

void testLocalRagAndLlmAreConnected() {
    Bm25Retriever bm25_retriever(
        "vector_db/chunks.json"
    );

    expectTrue(
        bm25_retriever.loadKnowledgeBase(),
        "load vehicle manual"
    );

    RecordingLlmBackend llm_backend;

    LocalRagLlmBackend backend(
        bm25_retriever,
        llm_backend
    );

    std::vector<RagStreamEvent> events;

    const RagStreamQueryResult result =
        backend.query(
            RagStreamRequest{
                "local-rag-1",
                "胎压报警应该怎么办"
            },
            [&events](const RagStreamEvent& event) {
                events.push_back(event);
            }
        );

    expectTrue(
        result.ok,
        "local RAG LLM query succeeds"
    );

    expectTrue(
        result.answer ==
            "请停车检查轮胎状态，并及时补充胎压。",
        "preserve final LLM answer"
    );

    expectTrue(
        result.backend == "cpp_local_rag_llm",
        "report local backend name"
    );

    expectTrue(
        result.llm_backend == "recording_llm",
        "report LLM backend name"
    );

    expectTrue(
        llm_backend.last_prompt.find(
            "胎压报警"
        ) != std::string::npos,
        "prompt contains retrieved manual content"
    );

    expectTrue(
        llm_backend.last_prompt.find(
            "胎压报警应该怎么办"
        ) != std::string::npos,
        "prompt contains user query"
    );

    expectTrue(
        events.size() == 4,
        "emit three chunks and one finished event"
    );

    expectTrue(
        events[0].type == RagStreamEventType::Chunk,
        "first event is chunk"
    );

    expectTrue(
        events[0].sequence == 0,
        "first event sequence is zero"
    );

    expectTrue(
        events[1].sequence == 1,
        "second event sequence is one"
    );

    expectTrue(
        events[2].sequence == 2,
        "third event sequence is two"
    );

    expectTrue(
        events[3].type ==
            RagStreamEventType::Finished,
        "last event is finished"
    );

    expectTrue(
        events[3].sequence == 3,
        "finished event follows all chunks"
    );

    expectTrue(
        result.timing.retrieval_elapsed_ms >= 0.0,
        "report retrieval elapsed time"
    );

    expectTrue(
        result.elapsed_ms >= 0.0,
        "report total backend time"
    );

    expectTrue(
        result.timing.first_token_observed,
        "observe first streamed token"
    );

    expectTrue(
        result.timing.llm_time_to_first_token_ms >= 0.0,
        "report LLM time to first token"
    );

    expectTrue(
        result.timing.llm_elapsed_ms >= result.timing.llm_time_to_first_token_ms,
        "LLM total time should not be less than TTFT"
    );

    expectTrue(
        result.elapsed_ms >= result.timing.retrieval_elapsed_ms,
        "backend total time should not be less than retrieval time"
    );
}

void testInvalidRequestIsRejected() {
    Bm25Retriever bm25_retriever(
        "vector_db/chunks.json"
    );

    expectTrue(
        bm25_retriever.loadKnowledgeBase(),
        "load manual for invalid request test"
    );

    RecordingLlmBackend llm_backend;

    LocalRagLlmBackend backend(
        bm25_retriever,
        llm_backend
    );

    const RagStreamQueryResult empty_id =
        backend.query(
            RagStreamRequest{
                "",
                "胎压报警"
            }
        );

    expectTrue(
        !empty_id.ok,
        "reject empty request_id"
    );

    const RagStreamQueryResult empty_query =
        backend.query(
            RagStreamRequest{
                "local-rag-2",
                ""
            }
        );

    expectTrue(
        !empty_query.ok,
        "reject empty query"
    );
}

void testBackendImplementsCancellationInterface() {
    Bm25Retriever bm25_retriever(
        "vector_db/chunks.json"
    );

    expectTrue(
        bm25_retriever.loadKnowledgeBase(),
        "load manual for cancellation test"
    );

    MockLlmBackend llm_backend;

    LocalRagLlmBackend backend(
        bm25_retriever,
        llm_backend
    );

    const CancellationResult result =
        backend.cancel("not-running");

    expectTrue(
        result.ok,
        "cancel request is valid"
    );

    expectTrue(
        !result.cancelled,
        "idle backend has nothing to cancel"
    );
}

}  // namespace

int main() {
    testLocalRagAndLlmAreConnected();
    testInvalidRequestIsRejected();
    testBackendImplementsCancellationInterface();

    if (failed_count == 0) {
        std::cout
            << "\nAll local RAG LLM backend tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}