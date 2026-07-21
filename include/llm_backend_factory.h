#pragma once

#include <memory>
#include <string>

#include "llm_backend.h"

struct LlmBackendOptions {
    std::string backend = "mock";
    std::string model_path;
    int max_new_tokens = 512;
    int max_context_len = 4096;
};

std::unique_ptr<LlmBackend> createLlmBackend(
    const LlmBackendOptions& options
);