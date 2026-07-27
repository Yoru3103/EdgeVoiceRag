#include <cstdint>
#include <iostream>
#include <string>
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

        return RagStreamQueryResult::success(
            request.request_id,
            final_answer,
            "mock_rag",
            "mock_llm",
            10.0
        );
    }
};

class MockTtsBackend final : public TtsBackend {
public:
    std::vector<std::string> synthesized_texts;

    bool fail = false;

    std::string name() const override {
        return "mock_tts";
    }

    TtsSynthesisResult synthesize(const std::string& text) override {
        if (fail) {
            return TtsSynthesisResult::failure("mock synthesis failure");
        }

        synthesized_texts.push_back(text);

        AudioBuffer audio;
        audio.sample_rate = 116000;
        audio.channels = 1;

        audio.samples = {
            static_cast<std::int16_t>(synthesized_texts.size())
        };

        return TtsSynthesisResult::success(std::move(audio));
    }
};

class MockAudioPlayer final : public AudioPlayer {
public:
    std::vector<AudioBuffer> played_audio;

    bool fail = false;
    bool stopped = false;

    std::string name() const override {
        return "mock_audio_player";
    }

    AudioPlaybackResult play(const AudioBuffer& audio) override {
        if (fail) {
            return AudioPlaybackResult::failure("mock playback failure");
        }

        played_audio.push_back(audio);

        return AudioPlaybackResult::success();
    }

    void stop() override {
        stopped = true;
    }
};

void testStreamedAnswerIsSynthesizedAndPlayed() {
    MockStreamingAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant(answer, tts, player);

    const VoiceAssistantResult result = assistant.processText(
        "voice-1",
        "胎压报警怎么办"
    );

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

    const auto result = assistant.processText(
        "voice-2",
        "无法启动车辆"
    );

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

    const auto result = assistant.processText(
        "voice-3",
        "测试"
    );

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

    const auto result =
        assistant.processText(
            "voice-4",
            "测试后端失败"
        );

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

    const auto result =
        assistant.processText(
            "voice-5",
            "测试TTS失败"
        );

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

    const auto result =
        assistant.processText(
            "voice-6",
            "测试播放失败"
        );

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

    assistant.stopPlayback();

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

    expectTrue(
        !empty_query.ok,
        "reject empty query"
    );
}

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