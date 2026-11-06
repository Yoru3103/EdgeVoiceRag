#include "local_rag_llm_backend.h"

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
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start);

    return elapsed.count();
}

}   // namespace

LocalRagLlmBackend::LocalRagLlmBackend(
    Retriever& retriever,
    LlmBackend& llm_backend,
    LocalRagLlmBackendConfig config
)
    : retriever_(retriever)
    , llm_backend_(llm_backend)
    , config_(std::move(config)) {
    if (config_.top_k <= 0) {
        throw std::invalid_argument(
            "local RAG top_k must be greater than zero"
        );
    }

    if (config_.system_prompt.empty()) {
        throw std::invalid_argument(
            "local RAG system prompt must not be empty"
        );
    }
}

std::string LocalRagLlmBackend::name() const {
    return "cpp_local_rag_llm";
}

RagStreamQueryResult LocalRagLlmBackend::query(
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

    /*
     * RKLLMHandle 通常不应同时运行多个推理请求。
     * VoiceAssistant 当前也只允许同时处理一个请求，
     * 这里再次检查是为了保护后端自身。
     */
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (active_) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "local RAG LLM backend is busy with request: " + active_request_id_
            );
        }

        active_ = true;
        active_request_id_ = request.request_id;
    }

    /*
     * 无论正常返回、生成失败还是发生异常，
     * 都必须清除 active_request_id_。
     */
    auto active_guard = makeScopeExit(
        [this, request_id = request.request_id]() {
            clearActiveRequest(request_id);
        }
    );

    std::size_t sequence = 0;
    std::string accumulated_answer;

    try {
        const auto retrieval_start = Clock::now();
        const std::vector<RetrievalResult> search_results = 
            retriever_.searchTopK(
                request.query,
                config_.top_k
            );

        timing.retrieval_elapsed_ms = elapsedMilliseconds(retrieval_start);

        const std::string prompt = buildPrompt(
            request.query,
            search_results
        );

        const auto llm_start = Clock::now();
        const LlmGenerationResult generation = 
            llm_backend_.generateStream(
                prompt,
                [
                    &handler,
                    &request,
                    &sequence,
                    &accumulated_answer,
                    &start,
                    &timing,
                    &llm_start,
                    this
                ](const std::string& chunk) {
                    if (chunk.empty()) {
                        return;
                    }

                    if (!timing.first_token_observed) {
                        timing.first_token_observed = true;

                        timing.llm_time_to_first_token_ms = elapsedMilliseconds(llm_start);
                    }

                    accumulated_answer += chunk;

                    RagStreamEvent event;
                    event.type = RagStreamEventType::Chunk;
                    event.ok = true;
                    event.sequence = sequence++;
                    event.request_id = request.request_id;
                    event.delta = chunk;
                    event.backend = name();
                    event.llm_backend = llm_backend_.name();
                    event.elapsed_ms = elapsedMilliseconds(start);
                    event.finished = false;

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
            error_event.backend = name();
            error_event.llm_backend = llm_backend_.name();
            error_event.error = generation.error;
            error_event.elapsed_ms = elapsedMilliseconds(start);
            error_event.finished = true;

            if (handler) {
                handler(error_event);
            }

            return RagStreamQueryResult::failure(
                request.request_id,
                generation.error,
                elapsedMilliseconds(start),
                timing
            );
        }

        /*
         * generateStream() 的最终 answer 应当等于所有
         * chunk 拼接后的结果。
         *
         * 该检查可以尽早发现 RKLLM 回调丢失文本、
         * 重复文本或者流式协议实现错误。
         */
        if (accumulated_answer != generation.answer) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "LLM stream chunks do not match final answer"
            );
        }

        RagStreamEvent finished_event;
        finished_event.type = RagStreamEventType::Finished;
        finished_event.backend = name();
        finished_event.ok = true;
        finished_event.sequence = sequence;
        finished_event.answer = generation.answer;
        finished_event.llm_backend = llm_backend_.name();
        finished_event.elapsed_ms = elapsedMilliseconds(start);
        finished_event.finished = true;

        if (handler) {
            handler(finished_event);
        }

        return RagStreamQueryResult::success(
            request.request_id,
            generation.answer,
            name(),
            llm_backend_.name(),
            finished_event.elapsed_ms,
            timing
        );
    } catch (const std::exception& error) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "local RAG LLM query failed: "
                + std::string(error.what()),
            elapsedMilliseconds(start),
            timing
        );
    } catch (...) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "local RAG LLM query failed with unknown error",
            elapsedMilliseconds(start),
            timing
        );
    }
}

CancellationResult LocalRagLlmBackend::cancel(const std::string& request_id) const {
    if (request_id.empty()) {
        return CancellationResult::failure(
            request_id,
            "request_id must not be empty"
        );
    }

    std::lock_guard<std::mutex> lock(state_mutex_);

    if (!active_) {
        return CancellationResult::success(
            request_id,
            false,
            ""
        );
    }

    if (active_request_id_ != request_id) {
        return CancellationResult::success(
            request_id,
            false,
            active_request_id_
        );
    }

    /*
     * MockLlmBackend 会设置 cancel_requested_；
     * RkllmBackend 会调用 rkllm_abort()。
     */
    const bool cancelled = llm_backend_.cancel();

    return CancellationResult::success(
        request_id,
        cancelled,
        active_request_id_
    );
}

std::string LocalRagLlmBackend::buildPrompt(
    const std::string& query,
    const std::vector<RetrievalResult>& results
) const {
    std::ostringstream prompt;

    prompt
        << config_.system_prompt
        << "\n\n"
        << "以下是从本地车辆手册中检索到的内容：\n";

    if (results.empty()) {
        prompt
            << "未检索到与该问题直接相关的车辆手册内容。\n";
    } else {
        for (std::size_t i = 0; i < results.size(); i++) {
            prompt
                << '['
                << i + 1
                << "] "
                << results[i].chunk.text
                << '\n';
        }
    }

    prompt
        << "\n用户问题: "
        << query
        << "\n\n"
        << "请直接给出适合语音播报的中文回答：";

    return prompt.str();
}

void LocalRagLlmBackend::clearActiveRequest(
    const std::string& request_id
) const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (active_ && active_request_id_ == request_id) {
        active_ = false;
        active_request_id_.clear();
    }
}
