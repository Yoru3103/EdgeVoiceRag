#include <iostream>
#include <stdexcept>
#include <string>

#include "llm_protocol.h"

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

void testRequestRoundTrip() {
    const LlmRequest original{
        "request-1",
        "蓝牙怎么连接",
        false
    };

    const std::string encoded = LlmProtocol::encodeRequest(original);
    const LlmRequest decoded = LlmProtocol::decodeRequest(encoded);

    expectTrue(
        decoded.request_id == original.request_id,
        "request_id round trip"
    );

    expectTrue(
        decoded.prompt == original.prompt,
        "prompt round trip"
    );

    expectTrue(
        decoded.stream == original.stream,
        "stream flag round trip"
    );
}

void testResponseRoundTrip() {
    const LlmResponse original{
        true,
        "request-2",
        "这是LLM回答",
        "mock",
        "",
        12.5,
        true
    };

    const std::string encoded = LlmProtocol::encodeResponse(original);
    const LlmResponse decoded = LlmProtocol::decodeResponse(encoded);

    expectTrue(decoded.ok, "response success");
    expectTrue(
        decoded.answer == original.answer,
        "answer round trip"
    );
    expectTrue(
        decoded.backend == "mock",
        "backend round trip"
    );
    expectTrue(
        decoded.finished,
        "response is finished"
    );
}

void testInvalidVersionRejected() {
    const std::string message = R"({
        "version": 99,
        "type": "generation_result",
        "ok": true,
        "request_id": "request-3",
        "answer": "answer"
    })";

    bool rejected = false;

    try {
        LlmProtocol::decodeResponse(message);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "invalid protocol version rejected"
    );
}

void testEmptyPromptRejected() {
    const LlmRequest request{
        "request-4",
        "",
        false
    };

    bool rejected = false;

    try {
        LlmProtocol::encodeRequest(request);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "empty prompt rejected"
    );
}

}  // namespace

int main() {
    testRequestRoundTrip();
    testResponseRoundTrip();
    testInvalidVersionRejected();
    testEmptyPromptRejected();

    if (failed_count == 0) {
        std::cout
            << "\nAll LLM protocol tests passed.\n";
        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}