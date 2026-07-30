#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "cancellable_answer_backend.h"
#include "llm_backend.h"
#include "rag_engine.h"
#include "streaming_answer_backend.h"

struct LocalRagLlmBackendConfig {
    int top_k = 3;

    std::string system_prompt = 
        "你是一个车载语音助手。"
        "请优先根据车辆手册内容回答问题。"
        "不要编造车辆功能、按钮位置或安全操作。"
        "如果手册中没有足够信息，请明确说明无法从当前车辆手册确认。"
        "回答应当简洁、自然，适合直接通过语音播报。";
};

// StreamingAnswerBackend负责流式生成；CancellableAnswerBackend负责打断生成，同一个对象实现两个功能
class LocalRagLlmBackend final
    : public StreamingAnswerBackend
    , public CancellableAnswerBackend {
public:
    LocalRagLlmBackend(
        RagEngine& rag_engine,
        LlmBackend& llm_backend,
        LocalRagLlmBackendConfig config = {}
    );

    std::string name() const override;

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler = {}
    ) const override;

    CancellationResult cancel(
        const std::string& request_id
    ) const override;

private:
    RagEngine& rag_engine_;
    LlmBackend& llm_backend_;
    LocalRagLlmBackendConfig config_;

    /*
     * LocalRagLlmBackend 对外提供 const query()，
     * 但请求执行状态会随着推理过程发生变化，
     * 因此这些成员需要使用 mutable。
     */
    mutable std::mutex state_mutex_;
    mutable bool active_ = false;
    mutable std::string active_request_id_;

    std::string buildPrompt(
        const std::string& query,
        const std::vector<SearchResult>& results
    ) const;

    void clearActiveRequest(
        const std::string& request_id
    ) const;
};
