#pragma once

#include <string>
#include <functional>

struct LlmGenerationResult {
    bool ok = false;
    std::string answer;
    std::string error;

    static LlmGenerationResult success(const std::string& answer) {
        return LlmGenerationResult{
            true,
            answer,
            ""
        };
    }

    static LlmGenerationResult failure(const std::string& error) {
        return LlmGenerationResult{
            false,
            "",
            error
        };
    }
};

using LlmChunkCallback = std::function<void(const std::string&)>;

// 定义所有LLM推理后端必须实现的接口
class LlmBackend {
public:
    virtual ~LlmBackend() = default;

    // =0 表示为纯虚函数，基类只规定接口，不提供派生类必须直接使用的默认实现。
    virtual std::string name() const = 0;
    virtual LlmGenerationResult generate(const std::string& prompt) = 0;

    virtual LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) {
        if (!callback) {
            return LlmGenerationResult::failure(
                "stream callback must not be empty"
            );
        }

        const LlmGenerationResult result = generate(prompt);

        if (result.ok && !result.answer.empty()) {
            callback(result.answer);
        }

        return result;
    }
};
