#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "asr_backend.h"
#include "audio_player.h"
#include "audio_recorder.h"
#include "streaming_answer_backend.h"
#include "tts_backend.h"
#include "voice_assistant.h"
#include "voice_input_pipeline.h"

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

AudioBuffer makeTestAudio() {
    AudioBuffer audio;

    audio.sample_rate = 16000;
    audio.channels = 1;

    audio.samples = {
        100,
        200,
        -100,
        -200
    };

    return audio;
}

class MockAudioRecorder final
    : public AudioRecorder {
public:
    AudioCaptureResult result =
        AudioCaptureResult::success(
            makeTestAudio(),
            12.0
        );

    int record_count = 0;
    bool stopped = false;

    std::string name() const override {
        return "mock_recorder";
    }

    AudioCaptureResult
    recordUtterance() override {
        ++record_count;
        return result;
    }

    void stop() override {
        stopped = true;
    }
};

class MockAsrBackend final
    : public AsrBackend {
public:
    AsrTranscriptionResult result =
        AsrTranscriptionResult::success(
            "胎压报警怎么办",
            25.0
        );

    int transcribe_count = 0;
    AudioBuffer last_audio;

    std::string name() const override {
        return "mock_asr";
    }

    AsrTranscriptionResult transcribe(
        const AudioBuffer& audio
    ) override {
        ++transcribe_count;
        last_audio = audio;

        return result;
    }
};

class MockAnswerBackend final
    : public StreamingAnswerBackend {
public:
    int query_count = 0;
    mutable std::string last_query;

    std::string name() const override {
        return "mock_answer";
    }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler
    ) const override {
        last_query = request.query;

        RagStreamEvent event;

        event.type =
            RagStreamEventType::Chunk;
        event.ok = true;
        event.request_id =
            request.request_id;
        event.sequence = 0;
        event.delta =
            "请检查车辆胎压。";
        event.backend = "mock_rag";
        event.llm_backend = "mock_llm";
        event.elapsed_ms = 1.0;
        event.finished = false;

        if (handler) {
            handler(event);
        }

        RagQueryTiming timing;
        timing.retrieval_elapsed_ms = 0.5;
        timing.llm_time_to_first_token_ms = 1.0;
        timing.llm_elapsed_ms = 1.5;
        timing.first_token_observed = true;

        return RagStreamQueryResult::success(
            request.request_id,
            "请检查车辆胎压。",
            "mock_rag",
            "mock_llm",
            2.0,
            timing
        );
    }
};

class MockTtsBackend final
    : public TtsBackend {
public:
    std::vector<std::string>
        synthesized_texts;

    std::string name() const override {
        return "mock_tts";
    }

    TtsSynthesisResult synthesize(
        const std::string& text
    ) override {
        synthesized_texts.push_back(text);

        AudioBuffer audio;
        audio.sample_rate = 16000;
        audio.channels = 1;
        audio.samples = {1};

        return TtsSynthesisResult::success(
            std::move(audio)
        );
    }
};

class MockAudioPlayer final
    : public AudioPlayer {
public:
    int play_count = 0;
    bool stopped = false;

    std::string name() const override {
        return "mock_player";
    }

    AudioPlaybackResult play(
        const AudioBuffer& audio
    ) override {
        if (audio.empty()) {
            return AudioPlaybackResult::failure(
                "empty audio"
            );
        }

        ++play_count;

        return AudioPlaybackResult::success();
    }

    void stop() override {
        stopped = true;
    }
};

struct TestSystem {
    MockAudioRecorder recorder;
    MockAsrBackend asr;
    MockAnswerBackend answer;
    MockTtsBackend tts;
    MockAudioPlayer player;

    VoiceAssistant assistant;
    VoiceInputPipeline input;

    TestSystem()
        : assistant(
              answer,
              tts,
              player
          ),
          input(
              recorder,
              asr,
              assistant
          ) {
    }
};

void testCompleteVoiceInputPipeline() {
    TestSystem system;

    expectTrue(
        system.assistant.start(),
        "start assistant"
    );

    const VoiceInputPipelineResult result =
        system.input.processOnce(
            "voice-input-1"
        );

    system.assistant.stop();

    expectTrue(
        result.ok,
        "voice input pipeline succeeds"
    );

    expectTrue(
        result.recognized_text ==
            "胎压报警怎么办",
        "preserve recognized text"
    );

    expectTrue(
        system.recorder.record_count == 1,
        "record one utterance"
    );

    expectTrue(
        system.asr.transcribe_count == 1,
        "run ASR once"
    );

    expectTrue(
        system.answer.last_query ==
            "胎压报警怎么办",
        "recognized text becomes RAG query"
    );

    expectTrue(
        system.tts.synthesized_texts.size() == 1,
        "answer is synthesized"
    );

    expectTrue(
        system.player.play_count == 1,
        "answer audio is played"
    );

    expectTrue(
        result.capture_elapsed_ms == 12.0,
        "preserve capture elapsed time"
    );

    expectTrue(
        result.asr_elapsed_ms == 25.0,
        "preserve ASR elapsed time"
    );

    expectTrue(
        result.assistant_result
            .answer_backend_elapsed_ms
            == 2.0,
        "preserve answer backend time"
    );

    expectTrue(
        result.assistant_result
            .rag_timing
            .retrieval_elapsed_ms
            == 0.5,
        "preserve retrieval time"
    );

    expectTrue(
        result.assistant_result
            .rag_timing
            .llm_time_to_first_token_ms
            == 1.0,
        "preserve LLM TTFT"
    );

    expectTrue(
        result.assistant_result
            .rag_timing
            .llm_elapsed_ms
            == 1.5,
        "preserve LLM total time"
    );
}

void testCaptureFailureStopsPipeline() {
    TestSystem system;

    system.recorder.result =
        AudioCaptureResult::failure(
            "microphone unavailable"
        );

    system.assistant.start();

    const auto result =
        system.input.processOnce(
            "voice-input-2"
        );

    system.assistant.stop();

    expectTrue(
        !result.ok,
        "capture failure is propagated"
    );

    expectTrue(
        result.error.find(
            "audio capture failed"
        ) != std::string::npos,
        "capture failure contains context"
    );

    expectTrue(
        system.asr.transcribe_count == 0,
        "capture failure skips ASR"
    );
}

void testInvalidAudioStopsBeforeAsr() {
    TestSystem system;

    AudioBuffer invalid_audio;
    invalid_audio.sample_rate = 16000;
    invalid_audio.channels = 1;

    system.recorder.result =
        AudioCaptureResult::success(
            std::move(invalid_audio)
        );

    system.assistant.start();

    const auto result =
        system.input.processOnce(
            "voice-input-3"
        );

    system.assistant.stop();

    expectTrue(
        !result.ok,
        "invalid audio is rejected"
    );

    expectTrue(
        result.error.find(
            "samples are empty"
        ) != std::string::npos,
        "invalid audio reports reason"
    );

    expectTrue(
        system.asr.transcribe_count == 0,
        "invalid audio skips ASR"
    );
}

void testAsrFailureStopsBeforeAnswer() {
    TestSystem system;

    system.asr.result =
        AsrTranscriptionResult::failure(
            "model inference failed"
        );

    system.assistant.start();

    const auto result =
        system.input.processOnce(
            "voice-input-4"
        );

    system.assistant.stop();

    expectTrue(
        !result.ok,
        "ASR failure is propagated"
    );

    expectTrue(
        result.error.find("ASR failed") !=
            std::string::npos,
        "ASR failure contains context"
    );

    expectTrue(
        system.answer.last_query.empty(),
        "ASR failure skips answer backend"
    );
}

void testEmptyTranscriptionIsRejected() {
    TestSystem system;

    system.asr.result =
        AsrTranscriptionResult::success(
            "",
            10.0
        );

    system.assistant.start();

    const auto result =
        system.input.processOnce(
            "voice-input-5"
        );

    system.assistant.stop();

    expectTrue(
        !result.ok,
        "empty transcription is rejected"
    );

    expectTrue(
        result.error.find("empty text") !=
            std::string::npos,
        "empty transcription reports reason"
    );
}

void testAssistantMustBeRunning() {
    TestSystem system;

    const auto result =
        system.input.processOnce(
            "voice-input-6"
        );

    expectTrue(
        !result.ok,
        "stopped assistant fails pipeline"
    );

    expectTrue(
        result.error.find(
            "voice assistant failed"
        ) != std::string::npos,
        "assistant failure contains context"
    );
}

void testStopRecording() {
    TestSystem system;

    system.input.stopRecording();

    expectTrue(
        system.recorder.stopped,
        "stopRecording stops recorder"
    );
}

void testRejectEmptyRequestId() {
    TestSystem system;

    const auto result =
        system.input.processOnce("");

    expectTrue(
        !result.ok,
        "reject empty input request_id"
    );

    expectTrue(
        system.recorder.record_count == 0,
        "invalid request skips recording"
    );
}

}  // namespace

int main() {
    testCompleteVoiceInputPipeline();
    testCaptureFailureStopsPipeline();
    testInvalidAudioStopsBeforeAsr();
    testAsrFailureStopsBeforeAnswer();
    testEmptyTranscriptionIsRejected();
    testAssistantMustBeRunning();
    testStopRecording();
    testRejectEmptyRequestId();

    if (failed_count == 0) {
        std::cout
            << "\nAll voice input pipeline "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
