#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio_player.h"
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

class MockStreamingAnswerBackend final : public StreamingAnswerBackend {
public:
    std::vector<std::string> chunks = {
        "请检查车",
        "辆胎压。然后重新",
        "启动车辆。"
    };

    std::string final_answer =
        "请检查车辆胎压。然后重新启动车辆。";

    bool fail = false;
    std::string failure_message =
        "mock answer failure";

    std::string name() const override {
        return "mock_answer";
    }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler
    ) const override {
        if (fail) {
            return RagStreamQueryResult::failure(
                request.request_id,
                failure_message
            );
        }

        std::size_t sequence = 0;

        for (const auto& chunk : chunks) {
            RagStreamEvent event;

            event.type =
                RagStreamEventType::Chunk;
            event.ok = true;
            event.request_id =
                request.request_id;
            event.sequence = sequence++;
            event.delta = chunk;
            event.backend = "mock_rag";
            event.llm_backend = "mock_llm";
            event.elapsed_ms =
                static_cast<double>(sequence);
            event.finished = false;

            if (handler) {
                handler(event);
            }
        }

        RagStreamEvent finished;

        finished.type =
            RagStreamEventType::Finished;
        finished.ok = true;
        finished.request_id =
            request.request_id;
        finished.sequence = sequence;
        finished.answer = final_answer;
        finished.backend = "mock_rag";
        finished.llm_backend = "mock_llm";
        finished.elapsed_ms = 10.0;
        finished.finished = true;

        if (handler) {
            handler(finished);
        }

        RagQueryTiming timing;
        timing.retrieval_elapsed_ms = 1.5;
        timing.llm_time_to_first_token_ms = 2.5;
        timing.llm_elapsed_ms = 8.0;
        timing.first_token_observed = true;

        return RagStreamQueryResult::success(
            request.request_id,
            final_answer,
            "mock_rag",
            "mock_llm",
            10.0,
            timing
        );
    }
};

class MockTtsBackend final : public TtsBackend {
public:
    std::vector<std::string> synthesized_texts;

    std::vector<std::thread::id> synthesis_thread_ids;

    std::mutex mutex;

    bool fail = false;

    std::string name() const override {
        return "mock_tts";
    }

    TtsSynthesisResult synthesize(const std::string& text) override {
        if (fail) {
            return TtsSynthesisResult::failure("mock synthesis failure");
        }

        {
            std::lock_guard<std::mutex> lock(mutex);

            synthesized_texts.push_back(text);

            synthesis_thread_ids.push_back(std::this_thread::get_id());
        }

        AudioBuffer audio;
        audio.sample_rate = 16000;
        audio.channels = 1;
        audio.samples = {1};

        return TtsSynthesisResult::success(std::move(audio));
    }
};

class MockAudioPlayer final : public AudioPlayer {
public:
    std::vector<AudioBuffer> played_audio;

    std::mutex mutex;

    bool fail = false;
    bool stopped = false;

    std::vector<std::thread::id> playback_thread_ids;

    std::string name() const override {
        return "mock_audio_player";
    }

    AudioPlaybackResult play(const AudioBuffer& audio) override {
        if (fail) {
            return AudioPlaybackResult::failure("mock playback failure");
        }

        {
            std::lock_guard<std::mutex> lock(mutex);

            played_audio.push_back(audio);

            playback_thread_ids.push_back(std::this_thread::get_id());
        }

        return AudioPlaybackResult::success();
    }

    void stop() override {
        stopped = true;
    }
};

class BlockingCancellableAnswerBackend final
    : public StreamingAnswerBackend
    , public CancellableAnswerBackend {
public:
    std::string name() const override {
        return "blocking_cancellable_mock";
    }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler
    ) const override {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            active_ = true;
            cancelled_ = false;
            active_request_id_ = request.request_id;
        }

        active_cv_.notify_all();

        RagStreamEvent event;

        event.type = RagStreamEventType::Chunk;
        event.ok = true;
        event.request_id = request.request_id;
        event.sequence = 0;
        event.delta = "正在生成";
        event.backend = "mock_rag";
        event.llm_backend = "mock_llm";
        event.elapsed_ms = 1.0;
        event.finished = false;

        if (handler) {
            handler(event);
        }

        {
            std::unique_lock<std::mutex> lock(mutex_);

            cancelled_cv_.wait(lock, [this]() {
                return cancelled_;
            });

            active_ = false;
            active_request_id_.clear();
        }

        return RagStreamQueryResult::failure(
            request.request_id,
            "generation cancelled"
        );
    }

    CancellationResult cancel(const std::string& request_id) const override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!active_ || active_request_id_ != request_id) {
            return CancellationResult::success(
                request_id,
                false,
                active_request_id_
            );
        }

        cancelled_ = true;

        cancelled_cv_.notify_all();

        return CancellationResult::success(
            request_id,
            true,
            request_id
        );
    }

    bool waitUntilActive(std::chrono::milliseconds timeout) const {
        std::unique_lock<std::mutex> lock(mutex_);

        return active_cv_.wait_for(
            lock,
            timeout,
            [this]() {
                return active_;
            }
        );
    }

private:
    mutable std::mutex mutex_;

    mutable std::condition_variable active_cv_;
    mutable std::condition_variable cancelled_cv_;

    mutable bool active_ = false;
    mutable bool cancelled_ = false;

    mutable std::string active_request_id_;
};

void testStreamedAnswerIsSynthesizedAndPlayed() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(answer, tts, player);

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const VoiceAssistantResult result = assistant.processText(
        "voice-1",
        "胎压报警怎么办"
    );

    assistant.stop();

    expectTrue(
        result.ok,
        "voice assistant succeeds"
    );
    expectTrue(
        result.answer ==
            "请检查车辆胎压。"
            "然后重新启动车辆。",
        "preserve final answer"
    );
    expectTrue(
        result.received_chunk_count == 3,
        "receive three chunks"
    );
    expectTrue(
        result.spoken_sentence_count == 2,
        "speak two sentences"
    );
    expectTrue(
        tts.synthesized_texts.size() == 2,
        "TTS receives two sentences"
    );
    expectTrue(
        tts.synthesized_texts[0] ==
            "请检查车辆胎压。",
        "TTS receives first sentence"
    );
    expectTrue(
        tts.synthesized_texts[1] ==
            "然后重新启动车辆。",
        "TTS receives second sentence"
    );
    expectTrue(
        player.played_audio.size() == 2,
        "player receives two audio buffers"
    );
    expectTrue(
        result.answer_backend_elapsed_ms
            == 10.0,
        "preserve answer backend elapsed time"
    );

    expectTrue(
        result.rag_timing
            .retrieval_elapsed_ms
            == 1.5,
        "preserve retrieval elapsed time"
    );

    expectTrue(
        result.rag_timing
            .llm_time_to_first_token_ms
            == 2.5,
        "preserve LLM TTFT"
    );

    expectTrue(
        result.rag_timing
            .llm_elapsed_ms
            == 8.0,
        "preserve LLM total time"
    );

    expectTrue(
        result.rag_timing.first_token_observed,
        "preserve first token state"
    );
}

void testUnpunctuatedRemainderIsFlushed() {
    MockStreamingAnswerBackend answer;

    answer.chunks = {
        "请联系",
        "服务中心"
    };

    answer.final_answer =
        "请联系服务中心";

    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const auto result = assistant.processText(
        "voice-2",
        "无法启动车辆"
    );

    assistant.stop();

    expectTrue(
        result.ok,
        "unpunctuated answer succeeds"
    );
    expectTrue(
        result.spoken_sentence_count == 1,
        "flush speaks final remainder"
    );
    expectTrue(
        tts.synthesized_texts[0] ==
            "请联系服务中心",
        "flush preserves remaining text"
    );
}

void testNonStreamingFallback() {
    MockStreamingAnswerBackend answer;

    answer.chunks.clear();
    answer.final_answer = "这是完整回答。";

    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const auto result = assistant.processText(
        "voice-3",
        "测试"
    );

    assistant.stop();

    expectTrue(
        result.ok,
        "non-streaming fallback succeeds"
    );
    expectTrue(
        tts.synthesized_texts.size() == 1,
        "final answer is synthesized"
    );
    expectTrue(
        tts.synthesized_texts[0] ==
            "这是完整回答。",
        "fallback uses final answer"
    );
}

void testAnswerBackendFailure() {
    MockStreamingAnswerBackend answer;
    answer.fail = true;

    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const auto result =
        assistant.processText(
            "voice-4",
            "测试后端失败"
        );

    assistant.stop();

    expectTrue(
        !result.ok,
        "answer failure is propagated"
    );
    expectTrue(
        result.error.find(
            "answer backend failed"
        ) != std::string::npos,
        "answer failure contains context"
    );
    expectTrue(
        tts.synthesized_texts.empty(),
        "answer failure does not call TTS"
    );
}

void testTtsFailure() {
    MockStreamingAnswerBackend answer;

    MockTtsBackend tts;
    tts.fail = true;

    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const auto result =
        assistant.processText(
            "voice-5",
            "测试TTS失败"
        );

    assistant.stop();

    expectTrue(
        !result.ok,
        "TTS failure is propagated"
    );

    expectTrue(
        result.error.find("TTS failed") !=
            std::string::npos,
        "TTS failure contains context"
    );

    expectTrue(
        player.played_audio.empty(),
        "TTS failure does not play audio"
    );
}

void testPlaybackFailure() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;

    MockAudioPlayer player;
    player.fail = true;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const auto result =
        assistant.processText(
            "voice-6",
            "测试播放失败"
        );

    assistant.stop();

    expectTrue(
        !result.ok,
        "playback failure is propagated"
    );

    expectTrue(
        result.error.find(
            "audio playback failed"
        ) != std::string::npos,
        "playback failure contains context"
    );
}

void testStopPlayback() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    assistant.stopPlayback();

    assistant.stop();

    expectTrue(
        player.stopped,
        "stopPlayback stops audio player"
    );
}

void testRejectEmptyInput() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start voice assistant"
    );

    const auto empty_id =
        assistant.processText(
            "",
            "测试"
        );

    expectTrue(
        !empty_id.ok,
        "reject empty request_id"
    );

    const auto empty_query =
        assistant.processText(
            "voice-7",
            ""
        );

    assistant.stop();

    expectTrue(
        !empty_query.ok,
        "reject empty query"
    );
}

void testPipelineUsesWorkerThreads() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start asynchronous pipeline"
    );

    const std::thread::id caller_thread = std::this_thread::get_id();

    const auto result = assistant.processText(
        "voice-async-1",
        "胎压报警怎么办"
    );

    assistant.stop();

    expectTrue(
        result.ok,
        "asynchronous pipeline succeeds"
    );
    expectTrue(
        !tts.synthesis_thread_ids.empty(),
        "TTS worker recorded thread"
    );
    expectTrue(
        !player.playback_thread_ids.empty(),
        "playback worker recorded thread"
    );
    expectTrue(
        tts.synthesis_thread_ids.front() != caller_thread,
        "TTS runs outside caller thread"
    );
    expectTrue(
        player.playback_thread_ids.front() != caller_thread,
        "playback runs outside caller thread"
    );
    expectTrue(
        tts.synthesis_thread_ids.front() != player.playback_thread_ids.front(),
        "TTS and playback use different workers"
    );
}

void testProcessRequiresStart() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    const auto result = assistant.processText(
        "voice-not-started",
        "测试"
    );

    expectTrue(
        !result.ok,
        "reject process before start"
    );
    expectTrue(
        result.error.find("not running") != std::string::npos,
        "not-running error contains reason"
    );
}

void testStopIsIdempotent() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start before repeated stop"
    );

    assistant.stop();
    assistant.stop();

    expectTrue(
        !assistant.running(),
        "repeated stop leaves assistant stopped"
    );
}

void testCannotRestartAfterStop() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start before repeated stop"
    );

    assistant.stop();
    
    expectTrue(
        !assistant.start(),
        "restart after stop is rejected"
    );
}

// 打断测试
void testStopPlaybackCancelsGeneration() {
    using namespace std::chrono_literals;

    BlockingCancellableAnswerBackend backend;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        backend,
        tts,
        player,
        backend
    );

    expectTrue(
        assistant.start(),
        "start cancellable assistant"
    );

    auto process_future = std::async(
        std::launch::async,
        [&assistant]() {
            return assistant.processText(
                "voice-cancel-1",
                "生成一个较长回答"
            );
        }
    );

    expectTrue(
        backend.waitUntilActive(1s),
        "answer backend becomes active"
    );

    const VoiceAssistantInterruptResult interrupted = assistant.stopPlayback();

    expectTrue(
        interrupted.had_active_request,
        "interrupt sees active request"
    );

    expectTrue(
        interrupted.request_id ==
            "voice-cancel-1",
        "interrupt uses active request_id"
    );

    expectTrue(
        interrupted.output_stopped,
        "interrupt stops local output"
    );

    expectTrue(
        interrupted.cancel_attempted,
        "interrupt attempts generation cancel"
    );

    expectTrue(
        interrupted.cancel_ok,
        "generation cancel request succeeds"
    );

    expectTrue(
        interrupted.generation_cancelled,
        "active generation is cancelled"
    );

    expectTrue(
        process_future.wait_for(1s) ==
            std::future_status::ready,
        "cancelled processText returns"
    );

    const VoiceAssistantResult result = process_future.get();

    expectTrue(
        !result.ok,
        "cancelled request returns failure"
    );

    expectTrue(
        result.error.find("interrupted") !=
            std::string::npos,
        "cancelled result reports interruption"
    );

    expectTrue(
        player.stopped,
        "audio player is stopped"
    );

    assistant.stop();
}

void testStopPlaybackWithoutActiveRequest() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(
        answer,
        tts,
        player
    );

    expectTrue(
        assistant.start(),
        "start assistant without active request"
    );

    const auto interrupted = assistant.stopPlayback();

    expectTrue(
        !interrupted.had_active_request,
        "no active request is reported"
    );

    expectTrue(
        interrupted.output_stopped,
        "local output stop still runs"
    );

    expectTrue(
        !interrupted.cancel_attempted,
        "no remote cancellation without request"
    );

    assistant.stop();
}

// 无活动请求
}   // namespace

int main() {
    testStreamedAnswerIsSynthesizedAndPlayed();
    testUnpunctuatedRemainderIsFlushed();
    testNonStreamingFallback();
    testAnswerBackendFailure();
    testTtsFailure();
    testPlaybackFailure();
    testStopPlayback();
    testRejectEmptyInput();
    testPipelineUsesWorkerThreads();
    testProcessRequiresStart();
    testStopIsIdempotent();
    testCannotRestartAfterStop();
    testStopPlaybackCancelsGeneration();

    if (failed_count == 0) {
        std::cout
            << "\nAll voice assistant tests "
            << "passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}