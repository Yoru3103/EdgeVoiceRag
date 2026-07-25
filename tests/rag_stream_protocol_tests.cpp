#include <iostream>
#include <stdexcept>
#include <string>

#include "rag_stream_protocol.h"

namespace {

int failed_count = 0;

void expectTrue(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

template <typename Exception, typename Function>
void expectThrows(Function function, const std::string& name) {
    try {
        function();
    } catch (const Exception&) {
        std::cout << "[PASS] " << name << '\n';
        return;
    } catch (...) {
        std::cout << "[FAIL] " << name << " threw wrong exception type\n";

        ++failed_count;
        return;
    }

    std::cout << "[FAIL] " << name << " did not throw\n";

    ++failed_count;
}

void testRequestRoundTrip() {
    const RagStreamRequest original{
        "rag-001",
        "空调怎么打开"
    };

    const std::string message = RagStreamProtocol::encodeRequest(original);

    const RagStreamRequest decoded = RagStreamProtocol::decodeRequest(message);

    expectTrue(
        decoded.request_id == original.request_id,
        "request_id round trip"
    );
    expectTrue(
        decoded.query == original.query,
        "query round trip"
    );
}

void testDecodeChunkEvent() {
    const std::string message = R"({
        "version": 1,
        "type": "rag_chunk",
        "ok": true,
        "request_id": "rag-001",
        "sequence": 0,
        "delta": "请打开空调控制界面。",
        "answer": "",
        "backend": "python_tfidf",
        "llm_backend": "cpp_mock",
        "error": "",
        "elapsed_ms": 12.5,
        "finished": false
    })";

    const RagStreamEvent event = RagStreamProtocol::decodeEvent(message);

    expectTrue(
        event.type == RagStreamEventType::Chunk,
        "decode chunk type"
    );
    expectTrue(
        event.request_id == "rag-001",
        "decode chunk request_id"
    );
    expectTrue(
        event.sequence == 0,
        "decode chunk sequence"
    );
    expectTrue(
        event.delta == "请打开空调控制界面。",
        "decode chunk delta"
    );
    expectTrue(
        !event.finished,
        "chunk is not finished"
    );
}

void testDecodeFinishedEvent() {
    const std::string message = R"({
        "version": 1,
        "type": "rag_finished",
        "ok": true,
        "request_id": "rag-001",
        "sequence": 1,
        "delta": "",
        "answer": "请打开空调控制界面。",
        "backend": "python_tfidf",
        "llm_backend": "cpp_mock",
        "error": "",
        "elapsed_ms": 30.0,
        "finished": true
    })";

    const RagStreamEvent event =
        RagStreamProtocol::decodeEvent(message);

    expectTrue(
        event.type == RagStreamEventType::Finished,
        "decode finished type"
    );
    expectTrue(
        event.answer == "请打开空调控制界面。",
        "decode final answer"
    );
    expectTrue(
        event.finished,
        "finished event is terminal"
    );
}

void testDecodeErrorEvent() {
    const std::string message = R"({
        "version": 1,
        "type": "rag_error",
        "ok": false,
        "request_id": "rag-001",
        "sequence": 1,
        "delta": "",
        "answer": "",
        "backend": "python_tfidf",
        "llm_backend": "cpp_mock",
        "error": "generation cancelled",
        "elapsed_ms": 15.0,
        "finished": true
    })";

    const RagStreamEvent event =
        RagStreamProtocol::decodeEvent(message);

    expectTrue(
        event.type == RagStreamEventType::Error,
        "decode error type"
    );
    expectTrue(
        event.error == "generation cancelled",
        "decode error message"
    );
    expectTrue(
        !event.ok,
        "error event has ok=false"
    );
}

void testRejectInvalidVersion() {
    const std::string message = R"({
        "version": 99,
        "type": "rag_chunk",
        "ok": true,
        "request_id": "rag-001",
        "sequence": 0,
        "delta": "test",
        "elapsed_ms": 1.0,
        "finished": false
    })";

    expectThrows<std::runtime_error>(
        [&message]() {
            RagStreamProtocol::decodeEvent(message);
        },
        "reject invalid protocol version"
    );
}

void testRejectNegativeSequence() {
    const std::string message = R"({
        "version": 1,
        "type": "rag_chunk",
        "ok": true,
        "request_id": "rag-001",
        "sequence": -1,
        "delta": "test",
        "elapsed_ms": 1.0,
        "finished": false
    })";

    expectThrows<std::runtime_error>(
        [&message]() {
            RagStreamProtocol::decodeEvent(message);
        },
        "reject negative sequence"
    );
}

void testRejectInvalidChunkState() {
    const std::string message = R"({
        "version": 1,
        "type": "rag_chunk",
        "ok": true,
        "request_id": "rag-001",
        "sequence": 0,
        "delta": "",
        "elapsed_ms": 1.0,
        "finished": false
    })";

    expectThrows<std::runtime_error>(
        [&message]() {
            RagStreamProtocol::decodeEvent(message);
        },
        "reject empty chunk"
    );
}

void testRejectEmptyRequestId() {
    const RagStreamRequest request{
        "",
        "空调怎么打开"
    };

    expectThrows<std::invalid_argument>(
        [&request]() {
            RagStreamProtocol::encodeRequest(request);
        },
        "reject empty request_id"
    );
}

void testRejectEmptyQuery() {
    const RagStreamRequest request{
        "rag-001",
        ""
    };

    expectThrows<std::invalid_argument>(
        [&request]() {
            RagStreamProtocol::encodeRequest(request);
        },
        "reject empty query"
    );
}

}   // namesace

int main() {
    testRequestRoundTrip();
    testDecodeChunkEvent();
    testDecodeFinishedEvent();
    testDecodeErrorEvent();

    testRejectInvalidVersion();
    testRejectNegativeSequence();
    testRejectInvalidChunkState();
    testRejectEmptyRequestId();
    testRejectEmptyQuery();

    if (failed_count == 0) {
        std::cout
            << "\nAll RAG stream protocol tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
