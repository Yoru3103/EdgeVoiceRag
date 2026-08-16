#include "voice_assistant.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

VoiceAssistantResult VoiceAssistantResult::failure(
    const std::string& request_id,
    const std::string& query,
    const std::string& error
) {
    VoiceAssistantResult result;

    result.ok = false;
    result.request_id = request_id;
    result.query = query;
    result.error = error;

    return result;
}

VoiceAssistant::VoiceAssistant(
    const StreamingAnswerBackend& answer_backend,
    TtsBackend& tts_backend,
    AudioPlayer& audio_player,
    std::size_t sentence_queue_capacity,
    std::size_t audio_queue_capacity
)
    : VoiceAssistant(
        answer_backend,
        tts_backend,
        audio_player,
        nullptr,
        sentence_queue_capacity,
        audio_queue_capacity
    ) {
}

VoiceAssistant::VoiceAssistant(
    const StreamingAnswerBackend& answer_backend,
    TtsBackend& tts_backend,
    AudioPlayer& audio_player,
    const CancellableAnswerBackend& cancellation_backend,
    std::size_t sentence_queue_capacity,
    std::size_t audio_queue_capacity
)
    : VoiceAssistant(
        answer_backend,
        tts_backend,
        audio_player,
        &cancellation_backend,
        sentence_queue_capacity,
        audio_queue_capacity
    ) {
}

VoiceAssistant::VoiceAssistant(
    const StreamingAnswerBackend& answer_backend,
    TtsBackend& tts_backend,
    AudioPlayer& audio_player,
    const CancellableAnswerBackend* cancellation_backend,
    std::size_t sentence_queue_capacity,
    std::size_t audio_queue_capacity
) 
    : answer_backend_(answer_backend)
    , tts_backend_(tts_backend)
    , audio_player_(audio_player) 
    , cancellation_backend_(cancellation_backend)
    , sentence_queue_(sentence_queue_capacity)
    , audio_queue_(audio_queue_capacity) {
}

VoiceAssistant::~VoiceAssistant() {
    stop();
}

bool VoiceAssistant::start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    if (started_ && !stopped_) {
        return true;
    }

    /*
     * 当前 BoundedBlockingQueue 关闭后不能 reopen，
     * 因此 stop() 之后不允许再次 start()。
     */
    if (stopped_) {
        return false;
    }

    /*
     * 创建两个常驻线程
     * 此时线程结构：
     * 主会话线程
     * TTS工作线程
     * 播放工作线程
     */
    try {
        // this表示让当前对象执行这个成员函数。类似异步执行
        tts_thread_ = std::thread(
            &VoiceAssistant::ttsWorker,
            this
        );

        playback_thread_ = std::thread(
            &VoiceAssistant::playbackWorker,
            this
        );

        started_ = true;

        return true;
    } catch (...) {
        // 关闭队列使得两个线程个走出阻塞
        sentence_queue_.close();
        audio_queue_.close();

        // joinable判断thread对象是否关联着一个可管理的线程
        if (tts_thread_.joinable()) {
            tts_thread_.join();     // 让主线程等待该线程执行完毕后再继续执行
        }

        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }

        stopped_ = true;

        return false;
    }
}

void VoiceAssistant::stop() {
    std::shared_ptr<RequestState> active_state;

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        if (stopped_) {
            return;
        }

        stopped_ = true;
    }

    // 依旧同上一版，先中断清空缓冲再关闭队列
    interruptActiveRequest("voice assistant stopped");

    sentence_queue_.close();
    audio_queue_.close();

    if (tts_thread_.joinable()) {
        tts_thread_.join();
    }
    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        started_ = false;
    }
}

bool VoiceAssistant::running() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    return started_ && !stopped_;
}

VoiceAssistantResult VoiceAssistant::processText(
    const std::string& request_id,
    const std::string& query
) {
    // v1.0: 只允许一次处理一个请求。
    // 避免多个回答共享buffer，多个回答争用一个player
    std::lock_guard<std::mutex> process_lock(process_mutex_);

    if (request_id.empty()) {
        return VoiceAssistantResult::failure(
            request_id,
            query,
            "request_id must not be empty"
        );
    }

    if (query.empty()) {
        return VoiceAssistantResult::failure(
            request_id,
            query,
            "query must not be empty"
        );
    }

    if (!running()) {
        return VoiceAssistantResult::failure(
            request_id,
            query,
            "voice assistant is not running"
        );
    }

    sentence_buffer_.clear();

    // 创建跨线程state
    // 同时被processText、TTS线程、播放线程和打断线程访问，因此使用sharedptr管理，只要任意任务仍然引用，就不会被释放
    const auto state = std::make_shared<RequestState>();

    // 由于停止时可能在任意线程中，因此需要添加request_id
    state->request_id = request_id;
    
    // 打断线程通过active_state_找到当前请求ID，取消正确的请求
    setActiveState(state);

    VoiceAssistantResult result;

    result.request_id = request_id;
    result.query = query;

    result.answer_backend = answer_backend_.name();
    result.tts_backend = tts_backend_.name();
    result.audio_backend = audio_player_.name();

    try {
        // 最外层Agent/RAG路由
        const RagStreamQueryResult answer_result = answer_backend_.query(
            RagStreamRequest{
                request_id,
                query
            },
            [this, &result, &state](const RagStreamEvent& event) {
                if (stateFailed(state) || event.type != RagStreamEventType::Chunk) {
                    return;
                }

                if (event.delta.empty()) {
                    return;
                }

                markFirstAnswerText(state);

                result.received_chunk_count++;

                const auto sentences = sentence_buffer_.append(event.delta);

                for (const auto& sentence : sentences) {
                    if (!enqueueSentence(state, sentence)) {
                        failState(state, "sentence queue closed");

                        return;
                    }
                }
            }
        );

        result.answer_backend_elapsed_ms = answer_result.elapsed_ms;
        result.rag_timing = answer_result.timing;
        result.response_mode = answer_result.response_mode;
        result.response_reason = answer_result.response_reason;
        result.query_category = answer_result.query_category;
        result.classification_confidence =
            answer_result.classification_confidence;
        result.retrieval_result_count =
            answer_result.retrieval_result_count;

        // state状态为真且结果已输出但结果为假
        if (!stateFailed(state) && !answer_result.ok) {
            failState(
                state,
                "answer backend failed: " + answer_result.error
            );
        }

        if (!stateFailed(state) && answer_result.ok) {
            result.answer = answer_result.answer;

            // 兼容不支持流式后端的情况（即不发送chunk的情况）
            // 如果没有chunk，就把完整答案送入缓冲器
            if (result.received_chunk_count == 0) {
                if (!answer_result.answer.empty()) {
                    markFirstAnswerText(state);
                }

                const auto sentences = sentence_buffer_.append(answer_result.answer);

                for (const auto& sentence : sentences) {
                    if (!enqueueSentence(state, sentence)) {
                        failState(state, "sentence queue closed");

                        break;
                    }
                }
            }

            if (!stateFailed(state)) {
                const std::string remaining = sentence_buffer_.flush();

                if (!remaining.empty() && !enqueueSentence(state, remaining)) {
                    failState(state, "sentence queue closed");
                }
            }
        }

        sentence_buffer_.clear();

        if (!enqueueEndOfRequest(state)) {
            failState(state, "failed to enqueue request end");

            completeState(state);
        }

        waitForCompletion(state);
    } catch (const std::exception& error) {
        sentence_buffer_.clear();

        failState(state, "voice assistant failed: " + std::string(error.what()));

        if (!enqueueEndOfRequest(state)) {
            completeState(state);
        } else {
            waitForCompletion(state);
        }
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);

        result.spoken_sentence_count = state->spoken_sentence_count;

        state->timing.total_elapsed_ms = state->timer.elapsedMilliseconds();
        result.timing = state->timing;

        if (state->failed) {
            result.ok = false;
            result.error = state->error;
        } else {
            result.ok = true;
            result.error.clear();
        }
    }

    clearActiveState(state);

    return result;
}

bool VoiceAssistant::enqueueSentence(
    const std::shared_ptr<RequestState>& state,
    const std::string& sentence
) {
    if (sentence.empty()) {
        return true;
    }

    SentenceTask task;
    task.state = state;
    task.text = sentence;
    task.end_of_request = false;

    return sentence_queue_.push(std::move(task));
}

bool VoiceAssistant::enqueueEndOfRequest(const std::shared_ptr<RequestState>& state) {
    SentenceTask task;

    task.state = state;
    task.end_of_request = true;

    return sentence_queue_.push(std::move(task));
}

void VoiceAssistant::ttsWorker() {
    while (true) {
        // pop通过notempty阻塞等待数据
        auto task = sentence_queue_.pop();

        if (!task.has_value()) {
            break;
        }

        if (task->end_of_request) {
            AudioTask end_task;

            end_task.state = task->state;
            end_task.end_of_request = true;

            if (!audio_queue_.push(std::move(end_task))) {
                failState(task->state, "audio queue closed");

                completeState(task->state);
            }

            continue;
        }

        if (stateFailed(task->state)) {
            continue;
        }

        PerfTimer tts_timer("tts_synthesis");

        try {
            TtsSynthesisResult synthesis = tts_backend_.synthesize(task->text);

            addTtsElapsed(
                task->state,
                tts_timer.elapsedMilliseconds()
            );

            if (!synthesis.ok) {
                failState(task->state, "TTS failed: " + synthesis.error);

                continue;
            }

            /*
             * TTS 期间可能发生 stopPlayback()。
             * 合成完成后再次检查状态，避免把旧音频入队。
             */
            if (stateFailed(task->state)) {
                continue;
            }

            markFirstAudioReady(task->state);

            AudioTask audio_task;
            audio_task.state = task->state;
            audio_task.audio = std::move(synthesis.audio);
            audio_task.end_of_request = false;

            if (!audio_queue_.push(std::move(audio_task))) {
                failState(task->state, "audio queue closed");

                completeState(task->state);

                break;
            }
        } catch (const std::exception& error) {
            failState(task->state, "TTS exception: " + std::string(error.what()));
        }
    }
}

void VoiceAssistant::playbackWorker() {
    while (true) {
        auto task = audio_queue_.pop();

        if (!task.has_value()) {
            break;
        }

        if (task->end_of_request) {
            completeState(task->state);
            continue;
        }

        if (stateFailed(task->state)) {
            continue;
        }

        markFirstPlaybackStarted(task->state);

        PerfTimer playback_timer("audio_playback");

        try {
            const AudioPlaybackResult playback = audio_player_.play(task->audio);

            addPlaybackElapsed(
                task->state,
                playback_timer.elapsedMilliseconds()
            );

            if (!playback.ok) {
                failState(task->state, "audio playback failed: " + playback.error);

                continue;
            }

            // 理由同ttsworeker
            if (!stateFailed(task->state)) {
                incrementSpokenCount(task->state);
            }
        } catch (const std::exception& error) {
            addPlaybackElapsed(
                task->state,
                playback_timer.elapsedMilliseconds()
            );

            failState(task->state, "audio playback exception: " + std::string(error.what()));
        }
    }
}

bool VoiceAssistant::stateFailed(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(state->mutex);

    return state->failed;
}

void VoiceAssistant::failState(
    const std::shared_ptr<RequestState>& state,
    const std::string& error
) {
    std::lock_guard<std::mutex> lock(state->mutex);

    // 保留第一次错误
    if (!state->failed) {
        state->failed = true;
        state->error = error;
    }
}

void VoiceAssistant::completeState(const std::shared_ptr<RequestState>& state) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);

        state->completed = true;
    }

    state->completed_cv.notify_all();
}

void VoiceAssistant::incrementSpokenCount(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(state->mutex);

    state->spoken_sentence_count++;
} 

void VoiceAssistant::waitForCompletion(const std::shared_ptr<RequestState>& state) {
    std::unique_lock<std::mutex> lock(state->mutex);

    state->completed_cv.wait(
        lock,
        [&state]() {
            return state->completed;
        }
    );
}

void VoiceAssistant::setActiveState(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(active_state_mutex_);

    active_state_ = state;
}

void VoiceAssistant::clearActiveState(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(active_state_mutex_);

    if (active_state_ == state) {
        active_state_.reset();
    }
}

VoiceAssistantInterruptResult VoiceAssistant::stopPlayback() {
    return interruptActiveRequest("voice output interrupted");
}

VoiceAssistantInterruptResult VoiceAssistant::interruptActiveRequest(const std::string& state_error) {
    VoiceAssistantInterruptResult result;

    std::shared_ptr<RequestState> state;

    {
        std::lock_guard<std::mutex> lock(active_state_mutex_);

        state = active_state_;
    }

    if (state) {
        result.had_active_request = true;

        {
            std::lock_guard<std::mutex> lock(state->mutex);

            result.request_id = state->request_id;
        }

        // 先标记失败，阻止TTS将刚合成完的旧音频重新放入audio_queue
        failState(state, state_error);

        // processText最终仍可能等待后端query返回，但不应该继续等待音频流水线完成
        completeState(state);
    }

    // 先停止本地输出
    // 因为网络取消可能还需要等待timeout_ms
    sentence_queue_.clear();
    audio_queue_.clear();

    audio_player_.stop();

    result.output_stopped = true;

    if (!state || !cancellation_backend_) {
        return result;
    }

    result.cancel_attempted = true;
    result.cancellation_backend = cancellation_backend_->name();

    try {
        // 网络取消存在阻塞，持锁期间无法清理active state，因此持锁复制shared_ptr，之后调用锁调用网络取消
        const CancellationResult cancellation = cancellation_backend_->cancel(result.request_id);

        result.cancel_ok = cancellation.ok;

        result.generation_cancelled = cancellation.cancelled;

        if (!cancellation.ok) {
            result.error = cancellation.error.empty() ? "generation cancellation failed" : cancellation.error;
        }
    } catch (const std::exception& error) {
        result.cancel_ok = false;
        result.generation_cancelled = false;

        result.error = "generation cancellation exception: " + std::string(error.what());
    } catch (...) {
        result.cancel_ok = false;
        result.generation_cancelled = false;

        result.error = "unknown generation cancellation exception";
    }

    return result;
}

void VoiceAssistant::markFirstAnswerText(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(state->mutex);

    if (!state->timing.first_answer_text_observed) {
        state->timing.first_answer_text_observed = true;
        state->timing.first_answer_text_ms = state->timer.elapsedMilliseconds();
    }
}

void VoiceAssistant::addTtsElapsed(
    const std::shared_ptr<RequestState>& state,
    double elapsed_ms) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->timing.tts_elapsed_ms += elapsed_ms;
}

void VoiceAssistant::markFirstAudioReady(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(state->mutex);

    if (!state->timing.first_audio_ready_observed) {
        state->timing.first_audio_ready_observed = true;
        state->timing.first_audio_ready_ms = state->timer.elapsedMilliseconds();
    }
}

void VoiceAssistant::markFirstPlaybackStarted(const std::shared_ptr<RequestState>& state) {
    std::lock_guard<std::mutex> lock(state->mutex);

    if (!state->timing.first_playback_started) {
        state->timing.first_playback_started = true;
        state->timing.first_playback_start_ms = state->timer.elapsedMilliseconds();
    }
}

void VoiceAssistant::addPlaybackElapsed(
    const std::shared_ptr<RequestState>& state,
    double elapsed_ms) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->timing.playback_elapsed_ms += elapsed_ms;
}
