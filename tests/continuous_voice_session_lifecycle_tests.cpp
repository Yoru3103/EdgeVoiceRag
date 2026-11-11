#include <algorithm>
#include <atomic>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "asr_backend.h"
#include "continuous_voice_session.h"
#include "interruptible_audio_recorder.h"
#include "scope_exit.h"
#include "support/voice_test_support.h"

namespace {
using namespace voice_test;

class NormalRecorder final : public AudioRecorder {
public:
    bool block = false;
    bool fail = false;
    int available_calls = 1;
    std::atomic_int calls{0};
    Event entered;
    Event released;

    std::string name() const override { return "scripted_normal"; }
    AudioCaptureResult recordUtterance() override {
        ++calls;
        entered.signal();
        if (block) {
            require(released.wait(), "normal recording not released");
            return AudioCaptureResult::failure("recording stopped");
        }
        if (fail || calls.load() > available_calls) {
            return AudioCaptureResult::failure("unexpected new normal recording");
        }
        return AudioCaptureResult::success(audio(old_marker), 12.0);
    }
    void stop() override { released.signal(); }
};

enum class BargeMode { NoSpeech, LatePublication, Callback, NoCallback };

class BargeRecorder final : public InterruptibleAudioRecorder {
public:
    BargeRecorder(BargeMode mode, const Answer& answer) : mode_(mode), answer_(answer) {}
    std::string name() const override { return "scripted_barge"; }
    AudioCaptureResult recordUtterance() override { return recordUtterance({}); }

    AudioCaptureResult recordUtterance(const SpeechStartedHandler& handler) override {
        int turn;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            turn = ++calls_;
            active_turn_ = turn;
            if (pending_cancel_) {
                cancelled_through_ = turn;
                pending_cancel_ = false;
            }
        }
        auto cleanup = makeScopeExit([&] {
            std::lock_guard<std::mutex> lock(mutex_);
            active_turn_ = 0;
        });
        if (turn == 1 && mode_ == BargeMode::LatePublication) {
            // A completed capture is published only after cancellation wakes
            // the recorder. This forces the answer-ready branch, without sleep
            // or dependence on relative thread speed.
            waitForCancellation(turn);
            if (handler) { handler(); }
            return AudioCaptureResult::success(audio(new_marker), 34.0);
        }
        if (turn == 1 && mode_ != BargeMode::NoSpeech) {
            require(answer_.generation_waiting.wait(), "answer not active before barge-in");
            if (mode_ == BargeMode::Callback && handler) { handler(); }
            return AudioCaptureResult::success(audio(new_marker), 34.0);
        }
        waitForCancellation(turn);
        return AudioCaptureResult::failure("barge recording cancelled");
    }

    void cancelCurrentRecording() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Preserve an early cancellation when the async task has not entered.
            if (active_turn_ > 0) {
                cancelled_through_ = active_turn_;
            } else {
                pending_cancel_ = true;
            }
        }
        cv_.notify_all();
    }
    int recordCallCount() const { return calls_.load(); }

    void stop() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

private:
    void waitForCancellation(int turn) {
        std::unique_lock<std::mutex> lock(mutex_);
        require(cv_.wait_for(lock, deadline, [&] {
            return stopped_ || cancelled_through_ >= turn;
        }), "barge recording not cancelled");
    }
    const BargeMode mode_;
    const Answer& answer_;
    std::atomic_int calls_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
    int cancelled_through_ = 0;
    bool stopped_ = false;
    int active_turn_ = 0;
    bool pending_cancel_ = false;
};

class Asr final : public AsrBackend {
public:
    Fault fault = Fault::None;
    bool empty = false;
    bool block = false;
    Event entered;
    Event released;
    std::vector<int> inputs;
    std::string name() const override { return "scripted_asr"; }
    AsrTranscriptionResult transcribe(const AudioBuffer& input) override {
        require(!input.samples.empty(), "ASR received empty input");
        inputs.push_back(input.samples.front());
        entered.signal();
        if (block) { require(released.wait(), "ASR not released"); }
        if (fault == Fault::Failure) {
            return AsrTranscriptionResult::failure("controlled ASR failure");
        }
        return AsrTranscriptionResult::success(
            empty ? "" : std::to_string(input.samples.front()), 25.0
        );
    }
};

struct SessionFixture {
    explicit SessionFixture(BargeMode mode, AnswerOptions options = {}, std::size_t turns = 2,
                            bool barge_enabled = true, PlayerOptions player_options = {})
        : answer(options), player(player_options), assistant(answer, tts, player, answer, 1, 1),
          barge(mode, answer), session(normal, barge, asr, assistant, {turns, 1, barge_enabled}) {}

    ~SessionFixture() {
        normal.stop();
        asr.released.signal();
        barge.stop();
        answer.generation_released.signal();
        session.stop();
        if (running.valid()) { running.wait(); }
    }

    void start() {
        running = std::async(std::launch::async, [&] {
            return session.run([&](const VoiceSessionEvent& event) {
                {
                    std::lock_guard<std::mutex> lock(event_mutex);
                    events.push_back(event);
                }
                if (stop_on_recognized && event.type == VoiceSessionEventType::RecognizedText) {
                    session.stop();
                }
            });
        });
    }

    ContinuousVoiceSessionResult finish() {
        require(running.wait_for(deadline) == std::future_status::ready, "session did not exit");
        return running.get();
    }

    NormalRecorder normal;
    Asr asr;
    Answer answer;
    Tts tts;
    Player player;
    VoiceAssistant assistant;
    BargeRecorder barge;
    ContinuousVoiceSession session;
    std::future<ContinuousVoiceSessionResult> running;
    bool stop_on_recognized = false;
    std::mutex event_mutex;
    std::vector<VoiceSessionEvent> events;
};

void captureSurvives(BargeMode mode) {
    AnswerOptions options;
    options.wait_for_cancel = mode != BargeMode::LatePublication;
    SessionFixture f(mode, options);
    f.start();
    const auto result = f.finish();
    require(result.ok, "session failed: " + result.error);
    require(result.completed_turns == 2 && result.interruption_count == 1,
            "interrupted/new turns not counted exactly once");
    require(f.normal.calls.load() == 1, "new speech was discarded and recorded again");
    require(f.asr.inputs == std::vector<int>({old_marker, new_marker}),
            "ASR did not reuse captured new speech");
    require(f.answer.requestCount() == 2, "new request missing or duplicated");
    require(!f.assistant.running(), "session did not join voice workers");

    std::vector<std::string> recognized;
    bool captured_timing = false;
    for (const auto& e : f.events) {
        if (e.type == VoiceSessionEventType::RecognizedText) {
            recognized.push_back(e.request_id);
            if (e.text == "202") { captured_timing = e.capture_elapsed_ms == 34.0; }
        }
    }
    require(recognized.size() == 2 && recognized[0] != recognized[1],
            "consecutive turns reused request_id");
    require(captured_timing, "pending capture timing was lost");
}

void recordingFailure() {
    SessionFixture f(BargeMode::NoSpeech, {}, 1);
    f.normal.fail = true;
    f.start();
    const auto result = f.finish();
    require(!result.ok && result.error.find("recording") != std::string::npos,
            "recording error missing");
    require(f.asr.inputs.empty() && f.answer.requestCount() == 0,
            "recording failure reached downstream stages");
    require(!f.assistant.running(), "recording failure left workers running");
}

void asrFailure(bool empty) {
    SessionFixture f(BargeMode::NoSpeech, {}, 1);
    f.asr.empty = empty;
    if (!empty) { f.asr.fault = Fault::Failure; }
    f.start();
    const auto result = f.finish();
    require(!result.ok && !result.error.empty(), "ASR error not reported");
    require(f.answer.requestCount() == 0, "invalid transcription reached answer backend");
    require(!f.assistant.running(), "ASR failure left workers running");
}

void answerFailure() {
    AnswerOptions options;
    options.answer_fault = Fault::Failure;
    SessionFixture f(BargeMode::NoSpeech, options, 1);
    f.start();
    const auto result = f.finish();
    require(!result.ok && result.error.find("answer") != std::string::npos,
            "answer failure was not propagated");
    require(!f.assistant.running(), "answer failure left workers running");
}

void stopDuringRecording() {
    SessionFixture f(BargeMode::NoSpeech);
    f.normal.block = true;
    f.start();
    require(f.normal.entered.wait(), "normal recording did not start");
    f.session.stop();
    const auto result = f.finish();
    require(result.ok && result.completed_turns == 0, "stopped recording advanced a turn");
    require(f.asr.inputs.empty() && !f.assistant.running(), "recording stop leaked work");
}

void stopDuringAnswer() {
    AnswerOptions options;
    options.wait_for_cancel = true;
    SessionFixture f(BargeMode::NoSpeech, options);
    f.start();
    require(f.answer.generation_waiting.wait(), "generation did not start");
    f.session.stop();
    const auto result = f.finish();
    require(result.ok && !f.assistant.running(), "stop did not reclaim session tasks");
}

void stopDuringAsr() {
    SessionFixture f(BargeMode::NoSpeech);
    f.asr.block = true;
    f.start();
    require(f.asr.entered.wait(), "ASR did not start");
    f.session.stop();
    f.asr.released.signal();
    const auto result = f.finish();
    require(result.ok && result.completed_turns == 0, "ASR stop advanced a turn");
    require(f.answer.requestCount() == 0, "stopped ASR launched a new answer task");
    require(!f.assistant.running(), "ASR stop left workers running");
}

void stopFromRecognizedCallback() {
    SessionFixture f(BargeMode::NoSpeech);
    f.stop_on_recognized = true;
    f.start();
    const auto result = f.finish();
    require(result.ok && result.completed_turns == 0, "callback stop advanced a turn");
    require(f.answer.requestCount() == 0, "stop in recognized callback launched answer task");
    require(!f.assistant.running(), "callback stop left workers running");
}

void halfDuplexCompletesNormalTurns() {
    SessionFixture f(BargeMode::Callback, {}, 2, false);
    f.normal.available_calls = 2;
    f.start();
    const auto result = f.finish();
    require(result.ok && result.completed_turns == 2 && result.interruption_count == 0,
            "half-duplex did not finish two ordinary turns");
    require(f.barge.recordCallCount() == 0, "disabled barge recorder was launched");
    require(f.normal.calls.load() == 2 && f.asr.inputs.size() == 2,
            "half-duplex did not obtain each question through normal recording");
    require(f.answer.requestCount() == 2 && !f.assistant.running(),
            "half-duplex lost a turn or left workers active");
    std::vector<std::string> ids;
    int completed = 0;
    for (const auto& e : f.events) {
        require(e.type != VoiceSessionEventType::BargeInDetected,
                "half-duplex emitted a barge-in event");
        if (e.type == VoiceSessionEventType::RecognizedText) ids.push_back(e.request_id);
        if (e.type == VoiceSessionEventType::AnswerCompleted) {
            ++completed;
            require(e.assistant_result.ok, "half-duplex reported a failed answer");
        }
    }
    require(ids.size() == 2 && ids[0] != ids[1] && completed == 2,
            "half-duplex IDs or completed events missing");
}

void halfDuplexAnswerFailure() {
    AnswerOptions options;
    options.answer_fault = Fault::Failure;
    SessionFixture f(BargeMode::Callback, options, 1, false);
    f.start();
    const auto result = f.finish();
    require(!result.ok && result.error.find("answer") != std::string::npos,
            "half-duplex swallowed the answer failure");
    require(f.barge.recordCallCount() == 0 && !f.assistant.running(),
            "half-duplex failure leaked recorder or workers");
}

void halfDuplexStopDuringAnswer() {
    AnswerOptions options;
    options.wait_for_cancel = true;
    SessionFixture f(BargeMode::Callback, options, 1, false);
    f.start();
    require(f.answer.generation_waiting.wait(), "half-duplex answer did not enter");
    f.session.stop();
    const auto result = f.finish();
    require(result.ok && f.barge.recordCallCount() == 0 && !f.assistant.running(),
            "half-duplex stop did not join the active answer");
    require(!f.answer.cancellations().empty(), "half-duplex stop did not cancel generation");
}

void halfDuplexStopDuringPlayback() {
    PlayerOptions playback;
    playback.block_old = true;
    SessionFixture f(BargeMode::Callback, {}, 1, false, playback);
    f.start();
    require(f.player.old_entered.wait(), "half-duplex playback did not enter");
    f.session.stop();
    const auto result = f.finish();
    require(result.ok && f.barge.recordCallCount() == 0 && !f.assistant.running(),
            "half-duplex stop did not reclaim playback workers");
    require(f.player.completed().empty(), "cancelled half-duplex audio completed");
}

void rejectInvalidConfig() {
    SessionFixture f(BargeMode::NoSpeech);
    bool rejected = false;
    try {
        ContinuousVoiceSession invalid(f.normal, f.barge, f.asr, f.assistant, {1, 0});
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "zero polling interval accepted");
}
}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> cases{
        {"completed_capture_survives_answer_first", [] { captureSurvives(BargeMode::LatePublication); }},
        {"speech_callback_preserves_next_capture", [] { captureSurvives(BargeMode::Callback); }},
        {"capture_without_callback_uses_fallback_cancel", [] { captureSurvives(BargeMode::NoCallback); }},
        {"recording_failure_stops_downstream", recordingFailure},
        {"asr_failure_stops_downstream", [] { asrFailure(false); }},
        {"empty_transcription_is_rejected", [] { asrFailure(true); }},
        {"answer_failure_reclaims_workers", answerFailure},
        {"stop_during_recording", stopDuringRecording},
        {"stop_during_answer", stopDuringAnswer},
        {"stop_during_asr", stopDuringAsr},
        {"stop_from_recognized_callback", stopFromRecognizedCallback},
        {"half_duplex_two_normal_turns", halfDuplexCompletesNormalTurns},
        {"half_duplex_answer_error", halfDuplexAnswerFailure},
        {"half_duplex_stop_during_answer", halfDuplexStopDuringAnswer},
        {"half_duplex_stop_during_playback", halfDuplexStopDuringPlayback},
        {"reject_zero_poll_interval", rejectInvalidConfig}
    };
    int failed = 0;
    for (const auto& c : cases) {
        try { c.second(); std::cout << "[PASS] " << c.first << '\n'; }
        catch (const std::exception& e) {
            ++failed; std::cerr << "[FAIL] " << c.first << ": " << e.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
