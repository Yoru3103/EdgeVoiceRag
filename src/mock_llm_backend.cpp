#include "mock_llm_backend.h"

#include <utility>
#include <algorithm>

namespace {

std::size_t findUtf8Boundary(
    const std::string& text,
    std::size_t maximum_size
) {
    std::size_t end = std::min(text.size(), maximum_size);

    while (
        end > 0 
        && end < text.size() 
        && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80
    ) {
        --end;
    }
    
    return end;
}

}   // namespace

MockLlmBackend::MockLlmBackend(std::size_t preview_length)
    : preview_length_(preview_length) {
}

std::string MockLlmBackend::name() const {
    return "cpp_mock";
}

LlmGenerationResult MockLlmBackend::generate(const std::string& prompt) {
    if (prompt.empty()) {
        return LlmGenerationResult::failure("prompt is empty");
    }

    std::string preview = prompt;

    if (preview.size() > preview_length_) {
        preview.resize(
            findUtf8Boundary(
                preview,
                preview_length_
            )
        );

        preview += "...";
    }

    return LlmGenerationResult::success(
        "这是C++ Mock LLM回答，收到的问题是：" + preview
    );
}