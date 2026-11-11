#include "continuous_voice_session.h"

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "scope_exit.h"

ContinuousVoiceSession::ContinuousVoiceSession(
    AudioRecorder& normal_recorder,
    InterruptibleAudioRecorder& barge_in_recorder,
    AsrBackend& asr_backend,
    VoiceAssistant& assistant,
    ContinuousVoiceSessionConfig config
)
    : normal_recorder_(normal_recorder)
    , asr_backend_(asr_backend)
    , barge_in_recorder_(barge_in_recorder)
    , assistant_(assistant)
    , config_(config) {
    if (config.future_poll_interval_ms <= 0) {
        throw std::invalid_argument(
            "future poll interval must be "
            "greater than zero"
        );
    }
}

ContinuousVoiceSessionResult ContinuousVoiceSession::run(
    const VoiceSessionEventHandler& handler
) {
    ContinuousVoiceSessionResult session_result;

    stop_requested_.store(false);

    // 启动语音助手
    if (!assistant_.start()) {
        session_result.error = "failed to start voice assistant";

        return session_result;
    }

    // 管理的是函数每次调用的生命周期
    // 当函数退出或意外结束时会调用析构函数结束，RAII
    // assistant为引用而不是函数内构建，函数崩溃不会调用assistant的析构函数
    auto assistant_guard = makeScopeExit(
        [this]() {
            assistant_.stop();
        }
    );

    /*
     * 用户打断时，VAD 已经获得了下一轮完整录音结果。
     * 保存 AudioCaptureResult，避免丢失录音耗时。
     */
    std::optional<AudioCaptureResult> pending_capture;
    AudioBuffer user_audio;

    std::size_t request_sequence = 0;

    while (!stop_requested_.load()) {
        if (
            config_.max_turns > 0
            && session_result.completed_turns >= config_.max_turns
        ) {
            break;
        }

        const std::string request_id = makeRequestId(++request_sequence);

        AudioCaptureResult capture;

        if (pending_capture.has_value()) {
            capture = std::move(pending_capture.value());
            pending_capture.reset();
        } else {
            // 先通知正在等待说话，然后调用录音器（非VAD）
            emitEvent(
                handler,
                VoiceSessionEvent{
                    VoiceSessionEventType::WaitingForSpeech,
                    request_id,
                    "",
                    ""
                }
            );

            capture = normal_recorder_.recordUtterance();

            if (stop_requested_.load()) {
                break;
            }
        }

        if (!capture.ok) {
            session_result.error =
                "normal recording failed: " + capture.error;

            emitEvent(
                handler,
                VoiceSessionEvent{
                    VoiceSessionEventType::Error,
                    request_id,
                    "",
                    session_result.error
                }
            );

            return session_result;
        }

        user_audio = std::move(capture.audio);
        const AsrTranscriptionResult transcription = asr_backend_.transcribe(user_audio);

        // stop()可能在不可中断的ASR调用期间到达，不再派发后续任务。
        if (stop_requested_.load()) {
            break;
        }

        if (!transcription.ok) {
            session_result.error = "ASR failed: " + transcription.error;

            emitEvent(
                handler,
                VoiceSessionEvent{
                    VoiceSessionEventType::Error,
                    request_id,
                    "",
                    session_result.error
                }
            );

            return session_result;
        }

        if (transcription.text.empty()) {
            session_result.error = "ASR returned empty text";

            return session_result;
        }

        VoiceSessionEvent recognized_event;
        recognized_event.type = VoiceSessionEventType::RecognizedText;
        recognized_event.request_id = request_id;
        recognized_event.text = transcription.text;
        recognized_event.capture_elapsed_ms = capture.elapsed_ms;
        recognized_event.asr_elapsed_ms = transcription.elapsed_ms;

        emitEvent(
            handler,
            std::move(recognized_event)
        );

        // 用户事件回调也可能同步调用stop()。
        if (stop_requested_.load()) {
            break;
        }

        // 回答异步执行；开启打断时再启动独立的监听任务。
        /*
         * processText 会一直等待：
         * LLM → TTS → 播放全部完成。
         *
         * 放入独立任务，以便启用时监听麦克风，同时保持stop()响应。
         */
        auto answer_future = std::async(
            std::launch::async,
            [
                this,
                request_id,
                text = transcription.text
            ]() {
                return assistant_.processText(
                    request_id,
                    text
                );
            }
        );

        // 确保回调和保护分支同时到达，打断逻辑也只执行一次
        std::atomic_bool barge_in_started{false};

        /*
         * 播放期间启动另一套高阈值 VAD。
         * 检测到起声时立即停止播放和 LLM，
         * 但 VAD 继续录制完整的新问题。
         */
        std::future<AudioCaptureResult> barge_future;
        if (config_.barge_in_enabled) {
            barge_future = std::async(
                std::launch::async,
                [
                    this,
                    &barge_in_started,
                    &handler,
                    request_id
                ]() {
                    return barge_in_recorder_.recordUtterance(
                        [
                            this,
                            &barge_in_started,
                            &handler,
                            request_id
                        ]() {
                            // 把原子变量切换为设定值，并返回原来的值
                            // 这里说明如果之前已为真，说明已经打断开始了
                            if (barge_in_started.exchange(true)) {
                                return;
                            }

                            const auto interrupt = assistant_.stopPlayback();

                            emitEvent(
                                handler,
                                VoiceSessionEvent{
                                    VoiceSessionEventType::BargeInDetected,
                                    request_id,
                                    "",
                                    interrupt.error
                                }
                            );
                        }
                    );
                }
            );
        }

        bool iteration_finished = false;

        while (
            !iteration_finished
            && !stop_requested_.load()
        ) {
            // 允许主线程非阻塞检查回答是否完成
            if (answer_future.wait_for(std::chrono::microseconds(0)) == std::future_status::ready) {
                const VoiceAssistantResult answer = answer_future.get();

                /*
                 * 回答任务已结束，回收打断任务并检查是否有下一轮录音。
                 */
                // 已起声时仍需录完新问题，回答结束不能取消这段录音。
                // 未观察到起声时取消监听，但录音可能已在并发边界完成；
                // 因此 get() 得到的成功结果也必须保存，不能直接丢弃。
                if (barge_future.valid()) {
                    if (!barge_in_started.load()) {
                        barge_in_recorder_.cancelCurrentRecording();
                    }

                    AudioCaptureResult barge_audio = barge_future.get();
                    if (barge_audio.ok) {
                        pending_capture = std::move(barge_audio);
                        session_result.completed_turns++;
                        session_result.interruption_count++;
                        iteration_finished = true;
                        continue;
                    }
                }

                if (!answer.ok && !barge_in_started.load()) {
                    session_result.error = "voice assistant failed: " + answer.error;

                    return session_result;
                }

                session_result.completed_turns++;

                VoiceSessionEvent completed_event;
                completed_event.type = VoiceSessionEventType::AnswerCompleted;
                completed_event.request_id = request_id;
                completed_event.text = answer.answer;
                completed_event.capture_elapsed_ms = capture.elapsed_ms;
                completed_event.asr_elapsed_ms = transcription.elapsed_ms;
                completed_event.assistant_result = answer;

                emitEvent(
                    handler,
                    std::move(completed_event)
                );

                iteration_finished = true;
                continue;
            }

            if (barge_future.valid()
                && barge_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                AudioCaptureResult barge_audio = barge_future.get();

                if (!barge_audio.ok) {
                    const VoiceAssistantResult answer = answer_future.get();

                    if (!answer.ok) {
                        session_result.error = "voice assistant failed: " + answer.error;

                        return session_result;
                    }

                    session_result.completed_turns++;
                    iteration_finished = true;
                    continue;
                }

                // 起声回调已经调用过stop，此处出于保护再调用一次
                if (!barge_in_started.load()) {
                    barge_in_started.store(true);
                    assistant_.stopPlayback();
                }

                const VoiceAssistantResult interrupted = answer_future.get();
                (void)interrupted;

                pending_capture = std::move(barge_audio);

                session_result.completed_turns++;
                session_result.interruption_count++;

                iteration_finished = true;
                continue;
            }

            // 避免纯忙轮询
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.future_poll_interval_ms));
        }

        if (stop_requested_.load()) {
            if (barge_future.valid()) {
                barge_in_recorder_.cancelCurrentRecording();
            }
            assistant_.stopPlayback();

            if (answer_future.valid()) {
                answer_future.wait();
            }

            if (barge_future.valid()) {
                barge_future.wait();
            }

            break;
        }
    }

    session_result.ok = session_result.error.empty();

    return session_result;
}

void ContinuousVoiceSession::stop() {
    stop_requested_.store(true);

    normal_recorder_.stop();
    if (config_.barge_in_enabled) {
        barge_in_recorder_.stop();
    }

    assistant_.stopPlayback();
}

std::string ContinuousVoiceSession::makeRequestId(std::size_t sequence) {
    return "voice-session" + std::to_string(sequence);
}

void ContinuousVoiceSession::emitEvent(
    const VoiceSessionEventHandler& handler,
    VoiceSessionEvent event
) {
    if (handler) {
        handler(event);
    }
}
