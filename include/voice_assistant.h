#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "audio_buffer.h"
#include "audio_player.h"
#include "bounded_blocking_queue.h"
#include "cancellable_answer_backend.h"
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

    // answer_backend的总耗时，对本地是检索+prompt+llm；对远程还会包含网络传输
    double answer_backend_elapsed_ms = 0.0;
    // 本地RAG/LLM的细分计时，远程后端保持默认零值
    RagQueryTiming rag_timing;

    static VoiceAssistantResult failure(
        const std::string& request_id,
        const std::string& query,
        const std::string& error
    );
};

struct VoiceAssistantInterruptResult {
    bool had_active_request = false;    // 调用时是否存在正在处理的请求
    bool output_stopped = false;        // 是否已执行本地播放停止

    bool cancel_attempted = false;      // 是否调用了生成取消后端
    bool cancel_ok = false;             // 取消请求成功
    bool generation_cancelled = false;  // 服务端取消请求成功

    std::string request_id;             // 请求ID
    std::string cancellation_backend;
    std::string error;                  // 错误信息
};

class VoiceAssistant {
public:
    // 使用引用是因为voiceassistant不拥有这些后端，只使用它们，需要保证这些后端的生命周期比assistant更长
    VoiceAssistant(
        const StreamingAnswerBackend& answer_backend,
        TtsBackend& tts_backend,
        AudioPlayer& audio_player,
        std::size_t sentence_queue_capacity = 4,
        std::size_t audio_queue_capacity = 2
    );

    VoiceAssistant(
        const StreamingAnswerBackend& answer_backend,
        TtsBackend& tts_backend,
        AudioPlayer& audio_player,
        const CancellableAnswerBackend& cancellation_backend,
        std::size_t sentence_queue_capacity = 4,
        std::size_t audio_queue_capacity = 2
    );

    ~VoiceAssistant();

    VoiceAssistant(const VoiceAssistant&) = delete;
    VoiceAssistant& operator=(const VoiceAssistant&) = delete;

    bool start();
    void stop();
    bool running() const;

    VoiceAssistantResult processText(
        const std::string& request_id,
        const std::string& query
    );

    VoiceAssistantInterruptResult stopPlayback();

private:
    // 负责线程之间状态的共享
    struct RequestState {
        mutable std::mutex mutex;
        std::condition_variable completed_cv;

        bool failed = false;
        bool completed = false;

        std::string request_id; // stopplayback()运行在其他线程，它需要知道当前应该取消哪个请求
        std::string error;

        std::size_t spoken_sentence_count = 0;
    };

    struct SentenceTask {
        std::shared_ptr<RequestState> state;
        std::string text;
        bool end_of_request = false;
    };

    struct AudioTask {
        std::shared_ptr<RequestState> state;
        AudioBuffer audio;
        bool end_of_request = false;
    };

    const StreamingAnswerBackend& answer_backend_;

    TtsBackend& tts_backend_;
    AudioPlayer& audio_player_;

    // 使用非拥有型指针，因为取消后端是可选的
    // 为nullptr时只停止本地输出，非nullptr时同时取消生成
    const CancellableAnswerBackend* cancellation_backend_ = nullptr;

    StreamingSentenceBuffer sentence_buffer_;

    BoundedBlockingQueue<SentenceTask> sentence_queue_;
    BoundedBlockingQueue<AudioTask> audio_queue_;

    std::thread tts_thread_;
    std::thread playback_thread_;

    mutable std::mutex lifecycle_mutex_;    // 保护started_, stopped_
    std::mutex process_mutex_;
    std::mutex active_state_mutex_;

    bool started_ = false;
    bool stopped_ = false;

    // 任务可能在processText结束前后跨线程传递
    // 必须保证状态对象在最后一个任务处理完之前仍然存在
    std::shared_ptr<RequestState> active_state_;    // 表示当前存在一个尚未完成的processtext请求

    VoiceAssistant(
        const StreamingAnswerBackend& answer_backend,
        TtsBackend& tts_backend,
        AudioPlayer& audio_player,
        const CancellableAnswerBackend* cancellation_backend,
        std::size_t sentence_queue_capacity,
        std::size_t audio_queue_capacity
    );

    VoiceAssistantInterruptResult interruptActiveRequest(const std::string& state_error);

    void ttsWorker();
    void playbackWorker();

    bool enqueueSentence(
        const std::shared_ptr<RequestState>& state,
        const std::string& sentence
    );

    static bool stateFailed(
        const std::shared_ptr<RequestState>& state
    );

    // 将流式后端生成的结果加入到sentencequeue中等待后续处理
    bool enqueueEndOfRequest(
        const std::shared_ptr<RequestState>& state
    );

    static void failState(
        const std::shared_ptr<RequestState>& state,
        const std::string& error
    );

    static void completeState(const std::shared_ptr<RequestState>& state);

    static void incrementSpokenCount(const std::shared_ptr<RequestState>& state);

    static void waitForCompletion(const std::shared_ptr<RequestState>& state);

    void setActiveState(const std::shared_ptr<RequestState>& state);

    void clearActiveState(const std::shared_ptr<RequestState>& state);
};
