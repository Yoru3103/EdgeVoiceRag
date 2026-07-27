#include "voice_assistant.h"

#include <exception>
#include <string>
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
    AudioPlayer& audio_player
) 
    : answer_backend_(answer_backend)
    , tts_backend_(tts_backend)
    , audio_player_(audio_player) {
}

VoiceAssistantResult VoiceAssistant::processText(
    const std::string& request_id,
    const std::string& query
) {
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

    sentence_buffer_.clear();

    VoiceAssistantResult result;

    result.request_id = request_id;
    result.query = query;

    result.answer_backend = answer_backend_.name();
    result.tts_backend = tts_backend_.name();
    result.audio_backend = audio_player_.name();

    bool output_failed = false;
    std::string output_error;

    try {
        const RagStreamQueryResult answer_result = answer_backend_.query(
            RagStreamRequest{
                request_id,
                query
            },
            [this, &result, &output_failed, &output_error](const RagStreamEvent& event) {
                if (output_failed || event.type != RagStreamEventType::Chunk) {
                    return;
                }

                result.received_chunk_count++;

                const std::vector<std::string> sentences = sentence_buffer_.append(event.delta);

                for (const auto& sentence : sentences) {
                    if (!synthesizeAndPlay(sentence, result)) {
                        output_failed = true;
                        output_error = result.error;
                        return;
                    }
                }
            }
        );

        if (output_failed) {
            sentence_buffer_.clear();

            result.ok = false;
            result.error = output_error;

            return result;
        }

        if (!answer_result.ok) {
            sentence_buffer_.clear();

            result.ok = false;
            result.error = "answer backend failed: " + answer_result.error;

            return result;
        }

        result.answer = answer_result.answer;

        // 兼容不支持流式后端的情况（即不发送chunk的情况）
        // 如果没有chunk，就把完整答案送入缓冲器
        if (result.received_chunk_count == 0) {
            const std::vector<std::string> sentences = sentence_buffer_.append(answer_result.answer);

            for (const auto& sentence : sentences) {
                if (!synthesizeAndPlay(sentence, result)) {
                    sentence_buffer_.clear();
                    return result;
                }
            }
        }

        const std::string remaining = sentence_buffer_.flush();

        if (!remaining.empty() && ! synthesizeAndPlay(remaining, result)) {
            return result;
        }

        result.ok = true;
        result.error.clear();

        return result;
    } catch (const std::exception& error) {
        sentence_buffer_.clear();

        result.ok = false;
        result.error = "voice assistant failed: " + std::string(error.what());

        return result;
    }
}

bool VoiceAssistant::synthesizeAndPlay(
    const std::string& sentence,
    VoiceAssistantResult& result
) {
    if (sentence.empty()) {
        return true;
    }

    const TtsSynthesisResult synthesis = tts_backend_.synthesize(sentence);

    if (!synthesis.ok) {
        result.ok = false;
        result.error = "TTS failed: " + synthesis.error;

        return false;
    }

    if (synthesis.audio.empty()) {
        result.ok = false;
        result.error = "TTS returned empty audio";

        return false;
    }

    const AudioPlaybackResult playback = audio_player_.play(synthesis.audio);

    if (!playback.ok) {
        result.ok = false;
        result.error = "audio playback failed: " + playback.error;

        return false;
    }

    result.spoken_sentence_count++;

    return true;
}

void VoiceAssistant::stopPlayback() {
    sentence_buffer_.clear();
    audio_player_.stop();
}
