#include "llm_backend_factory.h"

#include <stdexcept>

#include "mock_llm_backend.h"

#ifdef EDGE_ENABLE_RKLLM
#include "rkllm_backend.h"
#endif

std::unique_ptr<LlmBackend> createLlmBackend(
    const LlmBackendOptions& options
) {
    if (options.backend == "mock") {
        return std::make_unique<MockLlmBackend>();
    }

    if (options.backend == "rkllm") {
#ifdef EDGE_ENABLE_RKLLM
        RkllmBackendConfig config;
        config.model_path = options.model_path;
        config.max_new_tokens = options.max_new_tokens;
        config.max_context_len = options.max_context_len;

        return std::make_unique<RkllmBackend>(config);
#else
        throw std::runtime_error("RKLLM backend is not enabled in this build");
#endif
    }

    throw std::runtime_error("unsupported LLM backend: " + options.backend);
}