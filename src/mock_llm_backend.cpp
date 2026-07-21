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

LlmGenerationResult MockLlmBackend::generateStream(
    const std::string& prompt,
    const LlmChunkCallback& callback
) {
    if (!callback) {
        return LlmGenerationResult::failure(
            "stream callback must not be empty"
        );
    }

    const LlmGenerationResult result = generate(prompt);

    if (!result.ok) {
        return result;
    }

    constexpr std::size_t chunk_size = 12;
    std::size_t offset = 0;

    while (offset < result.answer.size()) {
        const std::string remaining = result.answer.substr(offset);

        const std::size_t current_size = findUtf8Boundary(remaining, chunk_size);

        if (current_size == 0) {
            return LlmGenerationResult::failure(
                "failed to find UTF-8 chunk boundary"
            );
        }

        callback(remaining.substr(0, current_size));

        offset += current_size;
    }


    return result;
}
