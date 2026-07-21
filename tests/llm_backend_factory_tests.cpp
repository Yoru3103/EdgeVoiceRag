#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

#include "llm_backend_factory.h"

namespace {

void testCreateMockBackend() {
    LlmBackendOptions options;
    options.backend = "mock";

    const auto backend = createLlmBackend(options);

    assert(backend != nullptr);
    assert(backend->name() == "cpp_mock");
}

void testRejectUnknownBackend() {
    LlmBackendOptions options;
    options.backend = "unknown";

    bool thrown = false;

    try {
        const auto backend = createLlmBackend(options);
        (void)backend;
    } catch (const std::runtime_error& error) {
        thrown = true;
        assert(
            std::string(error.what()).find("unsupported") !=
            std::string::npos
        );
    }

    assert(thrown);
}

#ifndef EDGE_ENABLE_RKLLM
void testRejectDisabledRkllmBackend() {
    LlmBackendOptions options;
    options.backend = "rkllm";
    options.model_path = "model.rkllm";

    bool thrown = false;

    try {
        const auto backend = createLlmBackend(options);
        (void)backend;
    } catch (const std::runtime_error& error) {
        thrown = true;
        assert(
            std::string(error.what()).find("not enabled") !=
            std::string::npos
        );
    }

    assert(thrown);
}
#endif

}  // namespace

int main() {
    testCreateMockBackend();
    testRejectUnknownBackend();

#ifndef EDGE_ENABLE_RKLLM
    testRejectDisabledRkllmBackend();
#endif

    std::cout << "llm_backend_factory_tests passed\n";
    return 0;
}