#pragma once

#include <cstddef>
#include <string>

#include "audio_player.h"
#include "streaming_answer_backend.h"
#include "streaming_sentence_buffer.h"
#include "tts_backend.h"

struct VoiceAssistantResult {
    bool ok = false;

    std::string request_id;
    std::string query;
    std::string answer;
    std::string error;

    std::string answer_backend;
    std::string tts_backend;
    std::string audio_backend;

    std::size_t received_chunk_count = 0;
    std::size_t spoken_sentence_count = 0;

    static VoiceAssistantResult failure(
        const std::string& request_id,
        const std::string& query,
        const std::string& error
    );
};

class VoiceAssistant {
public:
    // 使用引用是因为voiceassistant不拥有这些后端，只使用它们，需要保证这些后端的生命周期比assistant更长
    VoiceAssistant(
        const StreamingAnswerBackend& answer_backend,
        TtsBackend& tts_backend,
        AudioPlayer& audio_player
    );

    VoiceAssistantResult processText(
        const std::string& request_id,
        const std::string& query
    );

    void stopPlayback();

private:
    const StreamingAnswerBackend& answer_backend_;

    TtsBackend& tts_backend_;
    AudioPlayer& audio_player_;

    StreamingSentenceBuffer sentence_buffer_;

    bool synthesizeAndPlay(
        const std::string& sentence,
        VoiceAssistantResult& result
    );
};
