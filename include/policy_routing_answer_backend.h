#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "cancellable_answer_backend.h"
#include "llm_backend.h"
#include "query_classifier.h"
#include "response_policy.h"
#include "retriever.h"
#include "streaming_answer_backend.h"

struct PolicyRoutingAnswerBackendConfig {
    int top_k = 3;

    ResponsePolicyConfig policy;

    std::string rag_system_prompt =
        "你是一个车载语音助手。"
        "请优先根据车辆手册内容回答问题。"
        "不要编造车辆功能、按钮位置或安全操作。"
        "如果手册中没有足够信息，请明确说明无法从当前车辆手册确认。"
        "回答应当简洁、自然，适合直接通过语音播报。";

    std::string llm_only_system_prompt =
        "你是一个车载语音助手。"
        "当前问题不需要查询车辆手册。"
        "请给出简洁、自然、适合语音播报的中文回答。"
        "不要声称已经执行任何车辆控制操作。";
};

class PolicyRoutingAnswerBackend final
    : public StreamingAnswerBackend
    , public CancellableAnswerBackend {
public:
    PolicyRoutingAnswerBackend(
        Retriever& retriever,
        LlmBackend& llm_backend,
        PolicyRoutingAnswerBackendConfig config = {}
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
    Retriever& retriever_;
    LlmBackend& llm_backend_;
    PolicyRoutingAnswerBackendConfig config_;
    QueryClassifier classifier_;
    ResponsePolicy policy_;

    mutable std::mutex state_mutex_;
    mutable bool active_ = false;
    mutable std::string active_request_id_;

    std::string buildRagPrompt(
        const std::string& query,
        const std::vector<RetrievalResult>& results
    ) const;

    std::string buildLlmOnlyPrompt(
        const std::string& query
    ) const;

    static std::string buildDirectRagAnswer(
        const std::vector<RetrievalResult>& results
    );

    static std::string buildSafetyAnswer(
        const std::vector<RetrievalResult>& results
    );

    static std::string buildClarificationAnswer();

    void clearActiveRequest(
        const std::string& request_id
    ) const;
};
