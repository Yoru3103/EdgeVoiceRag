#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "asr_backend.h"
#include "audio_player.h"
#include "audio_recorder.h"
#include "continuous_voice_session.h"
#include "interruptible_audio_recorder.h"
#include "streaming_answer_backend.h"
#include "tts_backend.h"
#include "voice_assistant.h"

namespace {

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

AudioBuffer makeAudio() {
    AudioBuffer audio;
    audio.sample_rate = 16000;
    audio.channels = 1;
    audio.samples = {1, 2, 3, 4};

    return audio;
}

class MockNormalRecorder final : public AudioRecorder {
public:
    std::string name() const override {
        return "mock_normal_recorder";
    }

    AudioCaptureResult recordUtterance() override {
        return AudioCaptureResult::success(
            makeAudio(),
            12.0
        );
    }

    void stop() override {
    }
};

class MockBargeInRecorder final
    : public InterruptibleAudioRecorder {
public:
    std::string name() const override {
        return "mock_barge_in_recorder";
    }

    AudioCaptureResult recordUtterance() override {
        return recordUtterance({});
    }

    AudioCaptureResult recordUtterance(
        const SpeechStartedHandler& handler
    ) override {
        (void)handler;

        std::unique_lock<std::mutex> lock(mutex_);

        cancelled_cv_.wait(
            lock,
            [this]() {
                return cancelled_;
            }
        );

        return AudioCaptureResult::failure(
            "recording cancelled"
        );
    }

    void cancelCurrentRecording() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
        }

        cancelled_cv_.notify_all();
    }

    void stop() override {
        cancelCurrentRecording();
    }

private:
    std::mutex mutex_;
    std::condition_variable cancelled_cv_;
    bool cancelled_ = false;
};

class MockAsrBackend final : public AsrBackend {
public:
    std::string name() const override {
        return "mock_asr";
    }

    AsrTranscriptionResult transcribe(
        const AudioBuffer& audio
    ) override {
        if (audio.samples.empty()) {
            return AsrTranscriptionResult::failure(
                "empty audio"
            );
        }

        return AsrTranscriptionResult::success(
            "车里太热",
            25.0
        );
    }
};

class MockAnswerBackend final
    : public StreamingAnswerBackend {
public:
    std::string name() const override {
        return "mock_answer";
    }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler
    ) const override {
        RagStreamEvent chunk;
        chunk.type = RagStreamEventType::Chunk;
        chunk.ok = true;
        chunk.request_id = request.request_id;
        chunk.sequence = 0;
        chunk.delta = "正在为您降低空调温度。";
        chunk.backend = "mock_rag";
        chunk.llm_backend = "mock_llm";

        if (handler) {
            handler(chunk);
        }

        RagQueryTiming timing;
        timing.retrieval_elapsed_ms = 5.0;
        timing.llm_time_to_first_token_ms = 8.0;
        timing.llm_elapsed_ms = 20.0;
        timing.first_token_observed = true;

        return RagStreamQueryResult::success(
            request.request_id,
            "正在为您降低空调温度。",
            "mock_rag",
            "mock_llm",
            30.0,
            timing
        );
    }
};

class MockTtsBackend final : public TtsBackend {
public:
    std::string name() const override {
        return "mock_tts";
    }

    TtsSynthesisResult synthesize(
        const std::string& text
    ) override {
        if (text.empty()) {
            return TtsSynthesisResult::failure(
                "empty text"
            );
        }

        return TtsSynthesisResult::success(
            makeAudio()
        );
    }
};

class MockAudioPlayer final : public AudioPlayer {
public:
    std::string name() const override {
        return "mock_audio_player";
    }

    AudioPlaybackResult play(
        const AudioBuffer& audio
    ) override {
        if (audio.samples.empty()) {
            return AudioPlaybackResult::failure(
                "empty audio"
            );
        }

        return AudioPlaybackResult::success();
    }

    void stop() override {
    }
};

void testCompletedEventPreservesTiming() {
    MockNormalRecorder normal_recorder;
    MockBargeInRecorder barge_in_recorder;
    MockAsrBackend asr;
    MockAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    ContinuousVoiceSessionConfig config;
    config.max_turns = 1;
    config.future_poll_interval_ms = 1;

    ContinuousVoiceSession session(
        normal_recorder,
        barge_in_recorder,
        asr,
        assistant,
        config
    );

    std::vector<VoiceSessionEvent> events;

    const ContinuousVoiceSessionResult result =
        session.run(
            [&events](const VoiceSessionEvent& event) {
                events.push_back(event);
            }
        );

    expectTrue(
        result.ok,
        "continuous session succeeds"
    );

    const auto completed = std::find_if(
        events.begin(),
        events.end(),
        [](const VoiceSessionEvent& event) {
            return event.type ==
                VoiceSessionEventType::AnswerCompleted;
        }
    );

    expectTrue(
        completed != events.end(),
        "emit answer completed event"
    );

    if (completed == events.end()) {
        return;
    }

    expectTrue(
        completed->capture_elapsed_ms == 12.0,
        "preserve capture elapsed time"
    );

    expectTrue(
        completed->asr_elapsed_ms == 25.0,
        "preserve ASR elapsed time"
    );

    expectTrue(
        completed->assistant_result
                .answer_backend_elapsed_ms
            == 30.0,
        "preserve answer backend time"
    );

    expectTrue(
        completed->assistant_result
                .rag_timing
                .retrieval_elapsed_ms
            == 5.0,
        "preserve retrieval time"
    );

    expectTrue(
        completed->assistant_result
                .rag_timing
                .llm_time_to_first_token_ms
            == 8.0,
        "preserve LLM TTFT"
    );

    expectTrue(
        completed->assistant_result
                .timing
                .first_answer_text_observed,
        "preserve first answer text state"
    );

    expectTrue(
        completed->assistant_result
                .timing
                .first_audio_ready_observed,
        "preserve first audio ready state"
    );

    expectTrue(
        completed->assistant_result
                .timing
                .first_playback_started,
        "preserve first playback state"
    );
}

}  // namespace

int main() {
    testCompletedEventPreservesTiming();

    if (failed_count == 0) {
        std::cout
            << "\nAll continuous voice session tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
