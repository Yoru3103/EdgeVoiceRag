#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "llm_protocol.h"
#include "llm_stream_service.h"
#include "mock_llm_backend.h"

namespace {

class PartialFailingBackend : public LlmBackend {
public:
    std::string name() const override {
        return "partial_failing";
    }

    LlmGenerationResult generate(const std::string& prompt) override {
        (void) prompt;

        return LlmGenerationResult::failure("generation failed");
    }

    LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) override {
        (void)prompt;

        callback("第一段");

        return LlmGenerationResult::failure("generation failed");
    }
};

class CancellableBackend : public LlmBackend {
public:
    std::string name() const override {
        return "cancellable";
    }

    LlmGenerationResult generate(const std::string& prompt) override {
        (void)prompt;

        return LlmGenerationResult::failure("non-stream generation unused");
    }

    LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) override {
        (void)prompt;

        running_.store(true);
        cancelled_.store(false);

        callback("测试");

        while (!cancelled_.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
        }

        running_.store(false);

        return LlmGenerationResult::failure(
            "generation cancelled"
        );
    }

    bool cancel() override {
        if (!running_.load()) {
            return false;
        }

        cancelled_.store(true);
        return true;
    }

    bool isRunning() const {
        return running_.load();
    }

private:
    std::atomic_bool running_{false};
    std::atomic_bool cancelled_{false};
};

int failed_count = 0;

void expectTrue(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    failed_count++;
}

void testSuccessfulStream() {
    MockLlmBackend backend;
    LlmStreamService service(backend);

    const LlmRequest request {
        "stream-1",
        "空调怎么调节",
        true
    };

    std::vector<LlmStreamEvent> events;

    service.handleMessage(
        LlmProtocol::encodeRequest(request),
        [&events](const std::string& message) {
            events.push_back(
                LlmProtocol::decodeStreamEvent(message)
            );
        }
    );

    expectTrue(events.size() >= 2, "stream produces chunks and finish");

    std::string answer;
    std::size_t expected_sequence = 0;

    for (const LlmStreamEvent& event : events) {
        expectTrue(
            event.sequence == expected_sequence,
            "stream sequence is continuous"
        );

        if (event.type == LlmStreamEventType::Chunk) {
            answer += event.delta;
            expected_sequence++;
        }
    }

    const LlmStreamEvent& final_event = events.back();

    expectTrue(
        final_event.type == LlmStreamEventType::Finished,
        "last event is finished"
    );
    expectTrue(
        final_event.finished,
        "final event finished flag"
    );
    expectTrue(
        answer == final_event.answer,
        "chunks reconstruct final answer"
    );
}

void testNonStreamRequestRejected() {
    MockLlmBackend backend;
    LlmStreamService service(backend);

    const LlmRequest request{
        "stream-2",
        "测试非流式请求",
        false
    };

    std::vector<LlmStreamEvent> events;

    service.handleMessage(
        LlmProtocol::encodeRequest(request),
        [&events](const std::string& message) {
            events.push_back(
                LlmProtocol::decodeStreamEvent(message)
            );
        }
    );

    expectTrue(
        events.size() == 1,
        "non-stream request produces one error"
    );
    expectTrue(
        events.front().type == LlmStreamEventType::Error,
        "non-stream request is rejected"
    );
    expectTrue(
        events.front().finished,
        "error event is finished"
    );
}

void testFailureAfterChunkKeepsSequence() {
    PartialFailingBackend backend;
    LlmStreamService service(backend);

    const LlmRequest request{
        "stream-3",
        "测试生成失败",
        true
    };

    std::vector<LlmStreamEvent> events;

    service.handleMessage(
        LlmProtocol::encodeRequest(request),
        [&events](const std::string& message) {
            events.push_back(
                LlmProtocol::decodeStreamEvent(
                    message
                )
            );
        }
    );

    expectTrue(
        events.size() == 2,
        "partial failure produces chunk and error"
    );
    expectTrue(
        events[0].type == LlmStreamEventType::Chunk,
        "partial failure first event is chunk"
    );
    expectTrue(
        events[0].sequence == 0,
        "partial failure chunk sequence is zero"
    );
    expectTrue(
        events[1].type == LlmStreamEventType::Error,
        "partial failure last event is error"
    );
    expectTrue(
        events[1].sequence == 1,
        "partial failure error sequence continues"
    );
    expectTrue(
        events[1].finished,
        "partial failure error is terminal"
    );
}

void testCancelActiveRequest() {
    CancellableBackend backend;
    LlmStreamService service(backend);

    const LlmRequest request{
        "cancel-1",
        "测试取消",
        true
    };

    std::vector<LlmStreamEvent> events;

    std::thread generation_thread(
        [&]() {
            service.handleMessage(
                LlmProtocol::encodeRequest(request),
                [&events](const std::string& message) {
                    events.push_back(LlmProtocol::decodeStreamEvent(message));
                }
            );
        }
    );

    bool backend_started = false;

    for (int index = 0; index < 1000; index++) {
        if (backend.isRunning()) {
            backend_started = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    expectTrue(
        backend_started,
        "cancellable backend started"
    );

    const bool cancel_accepted = service.cancel(request.request_id);

    expectTrue(
        cancel_accepted,
        "active request cancellation accepted"
    );

    generation_thread.join();

    expectTrue(
        service.activeRequestId().empty(),
        "active request cleared after cancel"
    );

    expectTrue(
        !events.empty(),
        "cancel produces terminal event"
    );

    if (!events.empty()) {
        expectTrue(
            events.back().type == LlmStreamEventType::Error,
            "cancel ends with error event"
        );

        expectTrue(
            events.back().error == "generation cancelled",
            "cancel error reason is preserved"
        );
    }
}

}   // namespace

int main() {
    testNonStreamRequestRejected();
    testSuccessfulStream();
    testFailureAfterChunkKeepsSequence();
    testCancelActiveRequest();

    if (failed_count == 0) {
        std::cout << "\nAll LLM stream service tests passed.\n";
        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
