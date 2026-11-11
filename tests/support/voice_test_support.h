#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "audio_player.h"
#include "cancellable_answer_backend.h"
#include "streaming_answer_backend.h"
#include "tts_backend.h"
#include "voice_assistant.h"

// Test-only control points. A timeout fails the scenario; it is not used to
// choose thread order. Every blocking mock can be released during cleanup.
namespace voice_test {
using namespace std::chrono_literals;
constexpr auto deadline = 5s;
constexpr int old_marker = 101;
constexpr int new_marker = 202;

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Event {
public:
    void signal() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = true;
        }
        cv_.notify_all();
    }

    // Reset only after the preceding query has finished.
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
    }

    bool wait() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, deadline, [this] { return ready_; });
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    bool ready_ = false;
};

inline AudioBuffer audio(int marker) {
    AudioBuffer result;
    result.samples = {static_cast<std::int16_t>(marker)};
    return result;
}

enum class Fault { None, Failure, Exception };

struct AnswerOptions {
    int old_sentence_count = 1;
    bool wait_for_cancel = false;
    bool old_partial_before_wait = false;
    Fault answer_fault = Fault::None;
    Fault cancel_fault = Fault::None;
    const Event* first_tts_entered = nullptr;
};

class Answer final : public StreamingAnswerBackend, public CancellableAnswerBackend {
public:
    explicit Answer(AnswerOptions options = {}) : options_(options) {}

    std::string name() const override { return "controlled_answer"; }

    RagStreamQueryResult query(
        const RagStreamRequest& request,
        const RagStreamEventHandler& handler
    ) const override {
        const int marker = std::stoi(request.query);
        const bool old = marker == old_marker;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request.request_id);
        }
        if (!old) {
            new_query_entered.signal();
        }
        if (old && options_.answer_fault == Fault::Exception) {
            throw std::runtime_error("controlled answer exception");
        }
        if (old && options_.answer_fault == Fault::Failure) {
            return RagStreamQueryResult::failure(request.request_id, "controlled answer failure");
        }

        const int count = old ? options_.old_sentence_count : 1;
        std::string answer;
        for (int i = 0; i < count; ++i) {
            // Hold TTS sentence #1 before filling the capacity-one sentence
            // queue with #2. The #3 callback then encounters definite backpressure.
            if (old && i == 1 && options_.first_tts_entered) {
                require(options_.first_tts_entered->wait(), "first TTS did not enter");
            }
            if (old && i == 2) {
                third_chunk_entered.signal();
            }
            const std::string sentence = std::to_string(marker + i) + "。";
            answer += sentence;
            RagStreamEvent event;
            event.type = RagStreamEventType::Chunk;
            event.ok = true;
            event.request_id = request.request_id;
            event.sequence = static_cast<std::size_t>(i);
            event.delta = sentence;
            event.backend = name();
            event.llm_backend = "mock";
            if (handler) {
                handler(event);
            }
        }
        if (old && options_.old_partial_before_wait && handler) {
            RagStreamEvent partial;
            partial.type = RagStreamEventType::Chunk;
            partial.ok = true;
            partial.request_id = request.request_id;
            partial.sequence = static_cast<std::size_t>(count);
            partial.delta = "旧请求未完文本";
            handler(partial);
        }
        chunks_delivered.signal();
        if (old && options_.wait_for_cancel) {
            generation_waiting.signal();
            require(generation_released.wait(), "generation was not released");
        }
        return RagStreamQueryResult::success(
            request.request_id, answer, name(), "mock", 0.0
        );
    }

    CancellationResult cancel(const std::string& request_id) const override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancellations_.push_back(request_id);
        }
        if (options_.cancel_fault == Fault::Exception) {
            throw std::runtime_error("controlled cancellation exception");
        }
        if (options_.cancel_fault == Fault::Failure) {
            return CancellationResult::failure(request_id, "controlled cancellation failure");
        }
        generation_released.signal();
        return CancellationResult::success(request_id, true, request_id);
    }

    std::vector<std::string> cancellations() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancellations_;
    }

    std::size_t requestCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_.size();
    }

    mutable Event third_chunk_entered;
    mutable Event chunks_delivered;
    mutable Event generation_waiting;
    mutable Event generation_released;
    mutable Event new_query_entered;

private:
    const AnswerOptions options_;
    mutable std::mutex mutex_;
    mutable std::vector<std::string> requests_;
    mutable std::vector<std::string> cancellations_;
};

struct TtsOptions {
    bool block_old = false;
    Fault fault = Fault::None;
};

class Tts final : public TtsBackend {
public:
    explicit Tts(TtsOptions options = {}) : options_(options) {}

    std::string name() const override { return "controlled_tts"; }

    TtsSynthesisResult synthesize(const std::string& text) override {
        const int marker = std::stoi(text);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            synthesized_.push_back(marker);
        }
        if (marker == old_marker) {
            old_entered.signal();
            if (options_.block_old) {
                require(old_released.wait(), "old TTS was not released");
            }
            if (options_.fault == Fault::Exception) {
                throw std::runtime_error("controlled TTS exception");
            }
            if (options_.fault == Fault::Failure) {
                return TtsSynthesisResult::failure("controlled TTS failure");
            }
        }
        if (marker == old_marker + 2) {
            third_entered.signal();
        }
        return TtsSynthesisResult::success(audio(marker));
    }

    Event old_entered;
    Event old_released;
    Event third_entered;

private:
    const TtsOptions options_;
    std::mutex mutex_;
    std::vector<int> synthesized_;
};

struct PlayerOptions {
    bool block_old = false;
    Fault fault = Fault::None;
};

class Player final : public AudioPlayer {
public:
    explicit Player(PlayerOptions options = {}) : options_(options) {}

    std::string name() const override { return "controlled_player"; }

    AudioPlaybackResult play(const AudioBuffer& buffer) override {
        require(!buffer.samples.empty(), "empty test PCM");
        const int marker = buffer.samples.front();
        const unsigned entry_generation = stop_generation_.load();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_.push_back(marker);
        }
        if (marker == old_marker) {
            old_entered.signal();
            if (options_.block_old) {
                require(old_released.wait(), "old playback was not released");
            }
            if (options_.fault == Fault::Exception) {
                throw std::runtime_error("controlled player exception");
            }
            if (options_.fault == Fault::Failure) {
                return AudioPlaybackResult::failure("controlled player failure");
            }
        }
        if (entry_generation != stop_generation_.load()) {
            return AudioPlaybackResult::failure("playback stopped");
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back(marker);
        }
        return AudioPlaybackResult::success();
    }

    void stop() override {
        stop_generation_.fetch_add(1);
        old_released.signal();
        stopped.signal();
    }

    std::vector<int> started() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return started_;
    }

    std::vector<int> completed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_;
    }

    Event old_entered;
    Event old_released;
    Event stopped;

private:
    const PlayerOptions options_;
    std::atomic_uint stop_generation_{0};
    mutable std::mutex mutex_;
    std::vector<int> started_;
    std::vector<int> completed_;
};

inline VoiceAssistantResult take(std::future<VoiceAssistantResult>& future) {
    require(future.wait_for(deadline) == std::future_status::ready, "processText timed out");
    return future.get();
}

// Futures are members so cleanup releases mocks before waiting for their
// destructors, including on an assertion/exception path.
class Fixture {
public:
    Fixture(
        AnswerOptions answer_options = {},
        TtsOptions tts_options = {},
        PlayerOptions player_options = {}
    ) : answer(answer_options), tts(tts_options), player(player_options),
        assistant(answer, tts, player, answer, 1, 1) {
        require(assistant.start(), "assistant did not start");
    }

    ~Fixture() {
        release();
        assistant.stop();
        if (old.valid()) { old.wait(); }
        if (next.valid()) { next.wait(); }
    }

    void release() {
        tts.old_released.signal();
        answer.generation_released.signal();
        player.old_released.signal();
    }

    void startOld() {
        old = std::async(std::launch::async, [this] {
            return assistant.processText("old-request", std::to_string(old_marker));
        });
    }

    void startNext() {
        next = std::async(std::launch::async, [this] {
            return assistant.processText("new-request", std::to_string(new_marker));
        });
    }

    void checkNext() {
        startNext();
        const auto result = take(next);
        require(result.ok && result.request_id == "new-request", "new request did not succeed");
        require(result.spoken_sentence_count == 1, "new request did not play exactly one sentence");
        require(player.completed() == std::vector<int>{new_marker}, "old or missing PCM in completed output");
    }

    Answer answer;
    Tts tts;
    Player player;
    VoiceAssistant assistant;
    std::future<VoiceAssistantResult> old;
    std::future<VoiceAssistantResult> next;
};
}  // namespace voice_test
