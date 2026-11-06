#include "voice_performance_report.h"

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

constexpr const char* KReportType = "voice_turn_performance";

void validateEvent(const VoiceSessionEvent& event) {
    if (event.type != VoiceSessionEventType::AnswerCompleted) {
        throw std::invalid_argument(
            "voice performance report requires "
            "an AnswerCompleted event"
        );
    }

    if (event.request_id.empty()) {
        throw std::invalid_argument(
            "voice performance report request_id "
            "must not be empty"
        );
    }

    if (event.assistant_result.request_id != event.request_id) {
        throw std::invalid_argument(
            "voice performance report request_id "
            "does not match assistant result"
        );
    }
}

}   // namespace

std::string VoicePerformanceReport::encode(const VoiceSessionEvent& event) {
    validateEvent(event);

    const VoiceAssistantResult& assistant = event.assistant_result;
    const RagQueryTiming& rag = assistant.rag_timing;
    const VoiceAssistantTiming& voice = assistant.timing;

    /*
     * capture、ASR、processText 是顺序执行的，
     * 因此三者可以相加得到当前已观测到的整轮时间。
     *
     * answer_backend、TTS、playback 已包含在
     * assistant.total_elapsed_ms 中，不能再次相加。
     */
    const double turn_elapsed_ms = event.capture_elapsed_ms + event.asr_elapsed_ms + voice.total_elapsed_ms;

    const Json report = {
        {"version", KVersion},
        {"type", KReportType},

        {"request_id", event.request_id},
        {"ok", assistant.ok},

        {"answer_backend", assistant.answer_backend},
        {"tts_backend", assistant.tts_backend},
        {"audio_backend", assistant.audio_backend},

        {"response_mode", assistant.response_mode},
        {"response_reason", assistant.response_reason},
        {"query_category", assistant.query_category},
        {"classification_confidence", assistant.classification_confidence},
        {"retrieval_result_count", assistant.retrieval_result_count},

        {"received_chunk_count", assistant.received_chunk_count},
        {"spoken_sentence_count", assistant.spoken_sentence_count},

        {"capture_elapsed_ms", event.capture_elapsed_ms},
        {"asr_elapsed_ms", event.asr_elapsed_ms},

        {"answer_backend_elapsed_ms", assistant.answer_backend_elapsed_ms},

        {"retrieval_elapsed_ms", rag.retrieval_elapsed_ms},
        {"llm_first_token_observed", rag.first_token_observed},
        {"llm_time_to_first_token_ms", rag.llm_time_to_first_token_ms},
        {"llm_elapsed_ms", rag.llm_elapsed_ms},

        {"first_answer_text_observed", voice.first_answer_text_observed},
        {"first_answer_text_ms", voice.first_answer_text_ms},

        {"first_audio_ready_observed", voice.first_audio_ready_observed},
        {"first_audio_ready_ms", voice.first_audio_ready_ms},

        {"first_playback_started", voice.first_playback_started},
        {"first_playback_start_ms", voice.first_playback_start_ms},

        {"tts_elapsed_ms", voice.tts_elapsed_ms},
        {"playback_elapsed_ms", voice.playback_elapsed_ms},
        {"assistant_total_elapsed_ms", voice.total_elapsed_ms},
        {"turn_elapsed_ms", turn_elapsed_ms}
    };

    return report.dump();
}
