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

    if (!assistant_.start()) {
        session_result.error = "failed to start voice assistant";

        return session_result;
    }

    // 当函数退出或意外结束时会调用析构函数结束，RAII
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
        const AsrTranscriptionResult transcription =
            asr_backend_.transcribe(user_audio);

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

        /*
         * processText 会一直等待：
         * LLM → TTS → 播放全部完成。
         *
         * 因此必须放入单独线程，当前线程才能同时监听麦克风。
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

        std::atomic_bool barge_in_started{false};

        /*
         * 播放期间启动另一套高阈值 VAD。
         * 检测到起声时立即停止播放和 LLM，
         * 但 VAD 继续录制完整的新问题。
         */
        auto barge_future = std::async(
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

        bool iteration_finished = false;

        while (
            !iteration_finished
            && !stop_requested_.load()
        ) {
            if (answer_future.wait_for(std::chrono::microseconds(0)) == std::future_status::ready) {
                const VoiceAssistantResult answer = answer_future.get();

                /*
                 * 回答已自然结束，不再需要监听打断。
                 */
                barge_in_recorder_.cancelCurrentRecording();

                const AudioCaptureResult ignored = barge_future.get();

                (void)ignored;

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

            if (barge_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
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

            std::this_thread::sleep_for(std::chrono::milliseconds(config_.future_poll_interval_ms));
        }

        if (stop_requested_.load()) {
            barge_in_recorder_.cancelCurrentRecording();
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
    barge_in_recorder_.stop();

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
