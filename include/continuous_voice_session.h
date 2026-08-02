#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

#include "asr_backend.h"
#include "audio_recorder.h"
#include "interruptible_audio_recorder.h"
#include "voice_assistant.h"

enum class VoiceSessionEventType {
    WaitingForSpeech,
    RecognizedText,
    AnswerCompleted,
    BargeInDetected,
    Error,
};

struct VoiceSessionEvent {
    VoiceSessionEventType type = VoiceSessionEventType::Error;

    std::string request_id;
    std::string text;
    std::string error;
};

using VoiceSessionEventHandler = std::function<void(const VoiceSessionEvent&)>;

struct ContinuousVoiceSessionConfig {
    // 0表示无限循环，正数表示循环次数
    std::size_t max_turns = 0;

    int future_poll_interval_ms = 10;   // 异步任务轮询间隔
};

struct ContinuousVoiceSessionResult {
    bool ok = false;

    std::size_t completed_turns = 0;
    std::size_t interruption_count = 0;

    std::string error;
};

class ContinuousVoiceSession {
public:
    ContinuousVoiceSession(
        AudioRecorder& normal_recorder,                         // 语音录制
        InterruptibleAudioRecorder& barge_in_recorder,          // VAD
        AsrBackend& asr_backend,                                // ASR
        VoiceAssistant& assistant,                              // 处理ASR数据，完成LLM、TTS和音频播放
        ContinuousVoiceSessionConfig config = {}
    );

    ContinuousVoiceSessionResult run(
        const VoiceSessionEventHandler& handler = {}
    );

    void stop();

private:
    AudioRecorder& normal_recorder_;
    InterruptibleAudioRecorder& barge_in_recorder_;
    
    AsrBackend& asr_backend_;
    VoiceAssistant& assistant_;

    ContinuousVoiceSessionConfig config_;

    std::atomic_bool stop_requested_{false};                    // 停止标志

    static std::string makeRequestId(std::size_t sequence);

    static void emitEvent(
        const VoiceSessionEventHandler& handler,
        VoiceSessionEvent event
    );
};
