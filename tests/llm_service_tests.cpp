#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "llm_backend.h"
#include "llm_protocol.h"
#include "llm_service.h"
#include "mock_llm_backend.h"

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

class FailingLlmBackend : public LlmBackend {
public:
    std::string name() const override {
        return "failing";
    }

    LlmGenerationResult generate(const std::string& prompt) override {
        (void)prompt;
        return LlmGenerationResult::failure("inference failed");
    }
};

void testSuccessfulGeneration() {
    MockLlmBackend backend;
    LlmService service(backend);

    const LlmRequest request {
        "service-1",
        "你好",
        false
    };

    const std::string response_message = service.handleMessage(
        LlmProtocol::encodeRequest(request)
    );

    const LlmResponse response = LlmProtocol::decodeResponse(
        response_message
    );

    expectTrue(response.ok, "generation succeeds");
    expectTrue(
        response.request_id == request.request_id,
        "response preserves request_id"
    );
    expectTrue(
        response.backend == "cpp_mock",
        "response contains backend name"
    );
    expectTrue(
        response.answer.find("你好") != std::string::npos,
        "response contains prompt preview"
    );
    expectTrue(
        response.elapsed_ms >= 0.0,
        "response contains elapsed time"
    );
    expectTrue(response.finished, "response is finished");
}

void testBackendFailure() {
    FailingLlmBackend backend;
    LlmService service(backend);

    const LlmRequest request{
        "service-2",
        "测试失败",
        false
    };

    const LlmResponse response = LlmProtocol::decodeResponse(
        service.handleMessage(
            LlmProtocol::encodeRequest(request)
        )
    );

    expectTrue(!response.ok, "backend failure is returned");
    expectTrue(
        response.error == "inference failed",
        "backend failure keeps reason"
    );
    expectTrue(
        response.request_id == request.request_id,
        "failure preserves request_id"
    );
}

void testStreamingRejected() {
    MockLlmBackend backend;
    LlmService service(backend);

    const LlmRequest request{
        "service-3",
        "测试流式",
        true
    };

    const LlmResponse response = LlmProtocol::decodeResponse(
        service.handleMessage(
            LlmProtocol::encodeRequest(request)
        )
    );

    expectTrue(!response.ok, "streaming request is rejected");
    expectTrue(
        response.error.find("streaming")
            != std::string::npos,
        "streaming rejection contains reason"
    );
    expectTrue(
        response.finished,
        "failed response is finished"
    );
}

void testInvalidRequest() {
    MockLlmBackend backend;
    LlmService service(backend);

    const std::string invalid_request = R"({
        "version": 99,
        "type": "generate",
        "request_id": "service-4",
        "prompt": "invalid version"
    })";

    const LlmResponse response = LlmProtocol::decodeResponse(
        service.handleMessage(invalid_request)
    );

    expectTrue(!response.ok, "invalid request is rejected");
    expectTrue(
        response.request_id == "service-4",
        "invalid request preserves request_id"
    );
    expectTrue(
        response.finished,
        "failed response is finished"
    );
}

void testLongUtf8Prompt() {
    MockLlmBackend backend(10);
    LlmService service(backend);

    const LlmRequest request{
        "service-utf8",
        "这是一个很长的中文测试问题",
        false
    };

    const LlmResponse response = LlmProtocol::decodeResponse(
        service.handleMessage(
            LlmProtocol::encodeRequest(request)
        )
    );

    expectTrue(
        response.ok,
        "long UTF-8 prompt succeeds"
    );
    expectTrue(
        !response.answer.empty(),
        "long UTF-8 prompt returns answer"
    );
}

}   // namespace

int main() {
    testSuccessfulGeneration();
    testBackendFailure();
    testStreamingRejected();
    testInvalidRequest();
    testLongUtf8Prompt();

    if (failed_count == 0) {
        std::cout << "\nAll LLM service tests passed.\n";
        return 0;
    }

    std::cout << "\nFailed tests: " << failed_count << '\n';
    return 1;
}