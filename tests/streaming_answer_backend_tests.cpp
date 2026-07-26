#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "rag_stream_client.h"
#include "streaming_answer_backend.h"

namespace {

int failed_count;

void expectTrue(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

class MockStreamingAnswerBackend final
    : public StreamingAnswerBackend {
public:
    mutable int query_count = 0;
    mutable std::string last_query;
    mutable std::string last_request_id;

    std::string name() const override {
        return "mock_streaming_answer";
    }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler
    ) const override {
        query_count++;
        last_query = request.query;
        last_request_id = request.request_id;

        RagStreamEvent first_chunk;
        first_chunk.type = RagStreamEventType::Chunk;
        first_chunk.ok = true;
        first_chunk.request_id = request.request_id;
        first_chunk.sequence = 0;
        first_chunk.delta = "请检查";
        first_chunk.backend = "mock_rag";
        first_chunk.llm_backend = "mock_llm";
        first_chunk.elapsed_ms = 1.0;
        first_chunk.finished = false;

        RagStreamEvent second_chunk;
        second_chunk.type = RagStreamEventType::Chunk;
        second_chunk.ok = true;
        second_chunk.request_id = request.request_id;
        second_chunk.sequence = 1;
        second_chunk.delta = "车辆胎压。";
        second_chunk.backend = "mock_rag";
        second_chunk.llm_backend = "mock_llm";
        second_chunk.elapsed_ms = 2.0;
        second_chunk.finished = false;

        RagStreamEvent finished;
        finished.type = RagStreamEventType::Finished;
        finished.ok = true;
        finished.request_id = request.request_id;
        finished.sequence = 2;
        finished.answer = "请检查车辆胎压。";
        finished.backend = "mock_rag";
        finished.llm_backend = "mock_llm";
        finished.elapsed_ms = 3.0;
        finished.finished = true;

        if (handler) {
            handler(first_chunk);
            handler(second_chunk);
            handler(finished);
        }

        return RagStreamQueryResult::success(
            request.request_id,
            finished.answer,
            finished.backend,
            finished.llm_backend,
            finished.elapsed_ms
        );
    }
};

RagStreamQueryResult consumeBackend(
    const StreamingAnswerBackend& backend,
    const std::string& request_id,
    const std::string& query,
    std::vector<std::string>& chunks
) {
    return backend.query(
        RagStreamRequest(
            request_id,
            query
        ),
        [&chunks](const RagStreamEvent& event) {
            if (event.type == RagStreamEventType::Chunk) {
                chunks.push_back(event.delta);
            }
        }
    );
}

void testMockBackendThroughInterface() {
    MockStreamingAnswerBackend backend;

    std::vector<std::string> chunks;

    const RagStreamQueryResult result = consumeBackend(
        backend,
        "rag-interface-1",
        "胎压报警怎么办",
        chunks
    );

    expectTrue(
        result.ok,
        "query succeeds through interface"
    );
    expectTrue(
        result.answer == "请检查车辆胎压。",
        "interface preserves final answer"
    );
    expectTrue(
        chunks.size() == 2,
        "interface emits two chunks"
    );
    expectTrue(
        chunks[0] == "请检查",
        "first chunk is preserved"
    );
    expectTrue(
        chunks[1] == "车辆胎压。",
        "second chunk is preserved"
    );
    expectTrue(
        backend.query_count == 1,
        "mock backend receives one query"
    );
    expectTrue(
        backend.last_query == "胎压报警怎么办",
        "mock backend receives query text"
    );
    expectTrue(
        backend.last_request_id ==
            "rag-interface-1",
        "mock backend receives request_id"
    );
}

void testBackendName() {
    MockStreamingAnswerBackend backend;

    const StreamingAnswerBackend& interface = backend;

    expectTrue(
        interface.name() ==
            "mock_streaming_answer",
        "backend exposes implementation name"
    );
}

void testResultFactories() {
    const RagStreamQueryResult success =
        RagStreamQueryResult::success(
            "rag-1",
            "回答",
            "local_rag",
            "rkllm",
            10.0
        );

    expectTrue(
        success.ok,
        "success factory sets ok=true"
    );

    expectTrue(
        success.answer == "回答",
        "success factory preserves answer"
    );

    const RagStreamQueryResult failure =
        RagStreamQueryResult::failure(
            "rag-2",
            "generation failed"
        );

    expectTrue(
        !failure.ok,
        "failure factory sets ok=false"
    );

    expectTrue(
        failure.error == "generation failed",
        "failure factory preserves error"
    );
}

void testRagStreamClientImplementsInterface() {
    constexpr bool implements_interface =
        std::is_base_of_v<
            StreamingAnswerBackend,
            RagStreamClient
        >;

    expectTrue(
        implements_interface,
        "RagStreamClient implements "
        "StreamingAnswerBackend"
    );
}

} // namespace

int main() {
    testMockBackendThroughInterface();
    testBackendName();
    testResultFactories();
    testRagStreamClientImplementsInterface();

    if (failed_count == 0) {
        std::cout
            << "\nAll streaming answer backend "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
