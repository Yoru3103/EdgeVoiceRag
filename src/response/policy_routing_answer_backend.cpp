#include "policy_routing_answer_backend.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "scope_exit.h"

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(
        Clock::now() - start
    ).count();
}

std::string ensureTerminalPunctuation(std::string text) {
    if (text.empty()) {
        return text;
    }

    const auto ends_with = [&text](const std::string& suffix) {
        return text.size() >= suffix.size()
            && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    const char last = text.back();
    if (
        last != '.' && last != '!' && last != '?'
        && !ends_with("。") && !ends_with("！") && !ends_with("？")
        && !ends_with("；")
    ) {
        text += "。";
    }

    return text;
}

void applyDecisionMetadata(
    RagStreamQueryResult& result,
    const ResponsePolicyDecision& decision,
    const QueryClassifier& classifier
) {
    result.response_mode = ResponsePolicy::modeToString(decision.mode);
    result.response_reason = decision.reason;
    result.query_category = classifier.categoryToString(
        decision.classification.category
    );
    result.classification_confidence = decision.classification.confidence;
    result.retrieval_result_count = decision.retrieval_result_count;
}

}   // namespace

PolicyRoutingAnswerBackend::PolicyRoutingAnswerBackend(
    Retriever& retriever,
    LlmBackend& llm_backend,
    PolicyRoutingAnswerBackendConfig config
)
    : retriever_(retriever)
    , llm_backend_(llm_backend)
    , config_(std::move(config))
    , policy_(config_.policy) {
    if (config_.top_k <= 0) {
        throw std::invalid_argument(
            "policy routing top_k must be greater than zero"
        );
    }

    if (config_.rag_system_prompt.empty()) {
        throw std::invalid_argument(
            "policy routing RAG system prompt must not be empty"
        );
    }

    if (config_.llm_only_system_prompt.empty()) {
        throw std::invalid_argument(
            "policy routing LLM-only system prompt must not be empty"
        );
    }
}

std::string PolicyRoutingAnswerBackend::name() const {
    return "cpp_policy_router";
}

RagStreamQueryResult PolicyRoutingAnswerBackend::query(
    const RagStreamRequest& request,
    const RagStreamEventHandler& handler
) const {
    const auto start = Clock::now();
    RagQueryTiming timing;

    if (request.request_id.empty()) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "request_id must not be empty"
        );
    }

    if (request.query.empty()) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "query must not be empty"
        );
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (active_) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "another policy routing request is already active"
            );
        }

        active_ = true;
        active_request_id_ = request.request_id;
    }

    auto active_guard = makeScopeExit(
        [this, request_id = request.request_id]() {
            clearActiveRequest(request_id);
        }
    );

    try {
        const QueryClassification classification =
            classifier_.classify(request.query);

        ResponsePolicyDecision decision =
            policy_.decideWithoutRetrieval(classification);

        std::vector<RetrievalResult> results;

        if (decision.mode != PolicyResponseMode::LlmOnly) {
            const auto retrieval_start = Clock::now();
            results = retriever_.searchTopK(request.query, config_.top_k);
            timing.retrieval_elapsed_ms = elapsedMilliseconds(retrieval_start);
            decision = policy_.decideWithRetrieval(classification, results);
        }

        const std::string mode = ResponsePolicy::modeToString(decision.mode);

        if (
            decision.mode == PolicyResponseMode::Safety
            || decision.mode == PolicyResponseMode::DirectRag
            || decision.mode == PolicyResponseMode::Clarification
        ) {
            std::string answer;

            if (decision.mode == PolicyResponseMode::Safety) {
                answer = buildSafetyAnswer(results);
            } else if (decision.mode == PolicyResponseMode::DirectRag) {
                answer = buildDirectRagAnswer(results);
            } else {
                answer = buildClarificationAnswer();
            }

            RagStreamEvent chunk_event;
            chunk_event.type = RagStreamEventType::Chunk;
            chunk_event.ok = true;
            chunk_event.request_id = request.request_id;
            chunk_event.sequence = 0;
            chunk_event.delta = answer;
            chunk_event.backend = name() + "/" + mode;
            chunk_event.elapsed_ms = elapsedMilliseconds(start);

            if (handler) {
                handler(chunk_event);
            }

            RagStreamEvent finished_event;
            finished_event.type = RagStreamEventType::Finished;
            finished_event.ok = true;
            finished_event.request_id = request.request_id;
            finished_event.sequence = 1;
            finished_event.answer = answer;
            finished_event.backend = chunk_event.backend;
            finished_event.elapsed_ms = elapsedMilliseconds(start);
            finished_event.finished = true;

            if (handler) {
                handler(finished_event);
            }

            RagStreamQueryResult result = RagStreamQueryResult::success(
                request.request_id,
                answer,
                chunk_event.backend,
                "",
                elapsedMilliseconds(start),
                timing
            );
            applyDecisionMetadata(result, decision, classifier_);
            return result;
        }

        const std::string prompt = decision.mode == PolicyResponseMode::RagLlm
            ? buildRagPrompt(request.query, results)
            : buildLlmOnlyPrompt(request.query);

        std::size_t sequence = 0;
        std::string accumulated_answer;
        const auto llm_start = Clock::now();

        const LlmGenerationResult generation = llm_backend_.generateStream(
            prompt,
            [
                &handler,
                &request,
                &sequence,
                &accumulated_answer,
                &start,
                &llm_start,
                &timing,
                this,
                &mode
            ](const std::string& chunk) {
                if (chunk.empty()) {
                    return;
                }

                if (!timing.first_token_observed) {
                    timing.first_token_observed = true;
                    timing.llm_time_to_first_token_ms =
                        elapsedMilliseconds(llm_start);
                }

                accumulated_answer += chunk;

                RagStreamEvent event;
                event.type = RagStreamEventType::Chunk;
                event.ok = true;
                event.request_id = request.request_id;
                event.sequence = sequence++;
                event.delta = chunk;
                event.backend = name() + "/" + mode;
                event.llm_backend = llm_backend_.name();
                event.elapsed_ms = elapsedMilliseconds(start);

                if (handler) {
                    handler(event);
                }
            }
        );

        timing.llm_elapsed_ms = elapsedMilliseconds(llm_start);

        if (!generation.ok) {
            RagStreamEvent error_event;
            error_event.type = RagStreamEventType::Error;
            error_event.ok = false;
            error_event.request_id = request.request_id;
            error_event.sequence = sequence;
            error_event.backend = name() + "/" + mode;
            error_event.llm_backend = llm_backend_.name();
            error_event.error = generation.error;
            error_event.elapsed_ms = elapsedMilliseconds(start);
            error_event.finished = true;

            if (handler) {
                handler(error_event);
            }

            RagStreamQueryResult result = RagStreamQueryResult::failure(
                request.request_id,
                generation.error,
                elapsedMilliseconds(start),
                timing
            );
            applyDecisionMetadata(result, decision, classifier_);
            return result;
        }

        if (generation.answer.empty()) {
            RagStreamQueryResult result = RagStreamQueryResult::failure(
                request.request_id,
                "LLM generated an empty answer",
                elapsedMilliseconds(start),
                timing
            );
            applyDecisionMetadata(result, decision, classifier_);
            return result;
        }

        if (accumulated_answer != generation.answer) {
            RagStreamQueryResult result = RagStreamQueryResult::failure(
                request.request_id,
                "LLM stream chunks do not match final answer",
                elapsedMilliseconds(start),
                timing
            );
            applyDecisionMetadata(result, decision, classifier_);
            return result;
        }

        RagStreamEvent finished_event;
        finished_event.type = RagStreamEventType::Finished;
        finished_event.ok = true;
        finished_event.request_id = request.request_id;
        finished_event.sequence = sequence;
        finished_event.answer = generation.answer;
        finished_event.backend = name() + "/" + mode;
        finished_event.llm_backend = llm_backend_.name();
        finished_event.elapsed_ms = elapsedMilliseconds(start);
        finished_event.finished = true;

        if (handler) {
            handler(finished_event);
        }

        RagStreamQueryResult result = RagStreamQueryResult::success(
            request.request_id,
            generation.answer,
            finished_event.backend,
            llm_backend_.name(),
            elapsedMilliseconds(start),
            timing
        );
        applyDecisionMetadata(result, decision, classifier_);
        return result;
    } catch (const std::exception& error) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "policy routing failed: " + std::string(error.what()),
            elapsedMilliseconds(start),
            timing
        );
    }
}

CancellationResult PolicyRoutingAnswerBackend::cancel(
    const std::string& request_id
) const {
    if (request_id.empty()) {
        return CancellationResult::failure(
            request_id,
            "request_id must not be empty"
        );
    }

    std::lock_guard<std::mutex> lock(state_mutex_);

    if (!active_) {
        return CancellationResult::success(request_id, false, "");
    }

    if (active_request_id_ != request_id) {
        return CancellationResult::success(
            request_id,
            false,
            active_request_id_
        );
    }

    const bool cancelled = llm_backend_.cancel();

    return CancellationResult::success(
        request_id,
        cancelled,
        active_request_id_
    );
}

std::string PolicyRoutingAnswerBackend::buildRagPrompt(
    const std::string& query,
    const std::vector<RetrievalResult>& results
) const {
    std::ostringstream prompt;
    prompt << config_.rag_system_prompt
           << "\n\n以下是从本地车辆手册中检索到的内容：\n";

    for (std::size_t index = 0; index < results.size(); index++) {
        prompt << '[' << index + 1 << "] " << results[index].chunk.text << '\n';
    }

    prompt << "\n用户问题: " << query
           << "\n\n请直接给出适合语音播报的中文回答：";
    return prompt.str();
}

std::string PolicyRoutingAnswerBackend::buildLlmOnlyPrompt(
    const std::string& query
) const {
    return config_.llm_only_system_prompt
        + "\n\n用户问题: " + query
        + "\n\n请直接回答：";
}

std::string PolicyRoutingAnswerBackend::buildDirectRagAnswer(
    const std::vector<RetrievalResult>& results
) {
    if (results.empty()) {
        throw std::logic_error(
            "direct RAG response requires at least one retrieval result"
        );
    }

    return "根据车辆手册，" + ensureTerminalPunctuation(results.front().chunk.text);
}

std::string PolicyRoutingAnswerBackend::buildSafetyAnswer(
    const std::vector<RetrievalResult>& results
) {
    std::string answer =
        "这可能涉及行车安全。请保持对车辆的控制，在确认周围环境安全后将车辆停到安全位置。"
        "如果制动、转向或动力系统异常，请停止继续行驶并联系专业救援。";

    if (!results.empty()) {
        answer += "车辆手册相关说明：";
        answer += ensureTerminalPunctuation(results.front().chunk.text);
    } else {
        answer += "当前知识库没有足够信息确认具体故障原因。";
    }

    return answer;
}

std::string PolicyRoutingAnswerBackend::buildClarificationAnswer() {
    return
        "我还不能确定您想了解的具体内容。"
        "请说明车辆功能、仪表提示或遇到的现象，我再为您查询。";
}

void PolicyRoutingAnswerBackend::clearActiveRequest(
    const std::string& request_id
) const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (active_ && active_request_id_ == request_id) {
        active_ = false;
        active_request_id_.clear();
    }
}
