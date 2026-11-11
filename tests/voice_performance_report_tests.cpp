#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "voice_performance_report.h"

namespace {

using Json = nlohmann::json;

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

VoiceSessionEvent makeCompletedEvent() {
    VoiceSessionEvent event;

    event.type =
        VoiceSessionEventType::AnswerCompleted;

    event.request_id = "voice-session1";
    event.text = "正在为您降低空调温度。";

    event.capture_elapsed_ms = 1200.0;
    event.asr_elapsed_ms = 300.0;

    VoiceAssistantResult& assistant =
        event.assistant_result;

    assistant.ok = true;
    assistant.request_id = event.request_id;
    assistant.query = "车里太热";
    assistant.answer = event.text;

    assistant.answer_backend = "local_rag_llm";
    assistant.tts_backend = "sherpa_onnx_tts";
    assistant.audio_backend = "alsa";

    assistant.response_mode = "rag_llm";
    assistant.response_reason = "manual evidence requires generated synthesis";
    assistant.query_category = "complex";
    assistant.classification_confidence = 0.75F;
    assistant.retrieval_result_count = 3;

    assistant.received_chunk_count = 4;
    assistant.spoken_sentence_count = 1;

    assistant.answer_backend_elapsed_ms = 500.0;

    assistant.rag_timing.retrieval_elapsed_ms = 20.0;
    assistant.rag_timing.first_token_observed = true;
    assistant.rag_timing.llm_time_to_first_token_ms = 80.0;
    assistant.rag_timing.llm_elapsed_ms = 450.0;

    assistant.timing.first_answer_text_observed = true;
    assistant.timing.first_answer_text_ms = 100.0;

    assistant.timing.first_audio_ready_observed = true;
    assistant.timing.first_audio_ready_ms = 180.0;

    assistant.timing.first_playback_started = true;
    assistant.timing.first_playback_start_ms = 190.0;

    assistant.timing.tts_elapsed_ms = 70.0;
    assistant.timing.playback_elapsed_ms = 600.0;
    assistant.timing.total_elapsed_ms = 800.0;

    return event;
}

void testEncodeCompletedEvent() {
    const VoiceSessionEvent event =
        makeCompletedEvent();

    const std::string encoded =
        VoicePerformanceReport::encode(event);

    const Json report = Json::parse(encoded);

    expectTrue(
        report.at("version").get<int>() == 1,
        "encode report version"
    );

    expectTrue(
        report.at("type").get<std::string>()
            == "voice_turn_performance",
        "encode report type"
    );

    expectTrue(
        report.at("request_id").get<std::string>()
            == "voice-session1",
        "encode request id"
    );

    expectTrue(
        report.at("capture_elapsed_ms").get<double>()
            == 1200.0,
        "encode capture time"
    );

    expectTrue(
        report.at("asr_elapsed_ms").get<double>()
            == 300.0,
        "encode ASR time"
    );

    expectTrue(
        report.at("retrieval_elapsed_ms").get<double>()
            == 20.0,
        "encode retrieval time"
    );

    expectTrue(
        report.at("response_mode").get<std::string>()
            == "rag_llm",
        "encode response mode"
    );

    expectTrue(
        report.at("query_category").get<std::string>()
            == "complex",
        "encode query category"
    );

    expectTrue(
        report.at("retrieval_result_count").get<std::size_t>()
            == 3,
        "encode retrieval result count"
    );

    expectTrue(
        report.at("llm_time_to_first_token_ms").get<double>()
            == 80.0,
        "encode LLM TTFT"
    );

    expectTrue(
        report.at("first_playback_start_ms").get<double>()
            == 190.0,
        "encode first playback time"
    );

    expectTrue(
        report.at("assistant_total_elapsed_ms").get<double>()
            == 800.0,
        "encode assistant total time"
    );

    expectTrue(
        report.at("turn_elapsed_ms").get<double>()
            == 2300.0,
        "calculate measured turn time"
    );
}

void testQueueMetricsEncoding() {
    auto event = makeCompletedEvent();
    event.assistant_result.timing.queue_metrics_observed = true;
    event.assistant_result.timing.queue_metrics_complete = true;
    event.assistant_result.timing.sentence_queue = {4, 3, 2, 7.5};
    event.assistant_result.timing.audio_queue = {2, 2, 1, 3.5};
    const auto report = Json::parse(VoicePerformanceReport::encode(event));
    expectTrue(report.at("queue_metrics_complete").get<bool>()
               && report.at("sentence_queue_push_wait_ms").get<double>() == 7.5
               && report.at("audio_queue_peak_depth").get<int>() == 2,
               "encode observed queue metrics with units and depth");
    const auto legacy_shape = Json::parse(VoicePerformanceReport::encode(makeCompletedEvent()));
    expectTrue(!legacy_shape.at("queue_metrics_observed").get<bool>(),
               "default timing does not invent observed queue metrics");
}

void testRejectWrongEventType() {
    VoiceSessionEvent event = makeCompletedEvent();

    event.type =
        VoiceSessionEventType::RecognizedText;

    bool rejected = false;

    try {
        (void)VoicePerformanceReport::encode(event);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject non-completed event"
    );
}

void testRejectMismatchedRequestId() {
    VoiceSessionEvent event = makeCompletedEvent();

    event.assistant_result.request_id =
        "another-request";

    bool rejected = false;

    try {
        (void)VoicePerformanceReport::encode(event);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject mismatched request id"
    );
}

}  // namespace

int main() {
    testEncodeCompletedEvent();
    testQueueMetricsEncoding();
    testRejectWrongEventType();
    testRejectMismatchedRequestId();

    if (failed_count == 0) {
        std::cout
            << "\nAll voice performance report tests "
            << "passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
