#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "mock_llm_backend.h"

namespace {

void testStreamProducesChunks() {
    MockLlmBackend backend;

    std::vector<std::string> chunks;

    const LlmGenerationResult result =
    backend.generateStream(
        "空调温度怎么调节",
        [&chunks](const std::string& chunk) {
            assert(!chunk.empty());
            chunks.push_back(chunk);
        }
    );

    assert(result.ok);
    assert(!chunks.empty());

    std::string joined;

    for (const std::string& chunk : chunks) {
        joined += chunk;
    }

    assert(joined == result.answer);
}

void testStreamRejectsEmptyPrompt() {
    MockLlmBackend backend;

    int callback_count = 0;

    const LlmGenerationResult result =
        backend.generateStream(
            "",
            [&callback_count](const std::string&) {
                callback_count++;
            }
        );

    assert(!result.ok);
    assert(callback_count == 0);
}

void testBaseStreamCompatibility() {
    MockLlmBackend backend;

    LlmBackend& base = backend;
    std::string received;

    const LlmGenerationResult result =
        base.generateStream(
            "测试流式生成",
            [&received](const std::string& chunk) {
                received += chunk;
            }
        );

    assert(result.ok);
    assert(received == result.answer);
}

void testStreamRejectsEmptyCallback() {
    MockLlmBackend backend;
    const LlmChunkCallback callback;

    const LlmGenerationResult result =
        backend.generateStream(
            "测试空回调",
            callback
        );

    assert(!result.ok);
    assert(
        result.error ==
        "stream callback must not be empty"
    );
}

}   // namespace

int main() {
    testStreamProducesChunks();
    testStreamRejectsEmptyPrompt();
    testBaseStreamCompatibility();
    testStreamRejectsEmptyCallback();

    std::cout << "llm_backend_stream_tests passed\n";
    return 0;
}