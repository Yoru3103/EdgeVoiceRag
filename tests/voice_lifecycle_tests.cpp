#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#include <random>
#include <string>

#include "support/voice_test_support.h"
#include "scope_exit.h"

namespace {
using namespace voice_test;

void normalCompletion() {
    Fixture f;
    const auto result = f.assistant.processText("normal-request", "202");
    require(result.ok && result.spoken_sentence_count == 1, "normal completion failed");
    require(result.timing.queue_metrics_observed && result.timing.queue_metrics_complete,
            "successful queue metrics are not complete");
    require(result.timing.sentence_queue.capacity == 1 && result.timing.sentence_queue.peak_depth == 1
            && result.timing.audio_queue.capacity == 1 && result.timing.audio_queue.peak_depth == 1,
            "data queue capacities/peaks are not per-request");
    require(f.player.completed() == std::vector<int>{new_marker}, "normal PCM mismatch");
}

void destructorReclaimsWorkers() {
    Answer answer;
    Tts tts;
    Player player;
    for (int i = 0; i < 2; ++i) {
        auto assistant = std::make_unique<VoiceAssistant>(answer, tts, player, answer, 1, 1);
        require(assistant->start(), "destructor test could not start workers");
        const auto result = assistant->processText("destructor-request-" + std::to_string(i), "202");
        require(result.ok && result.spoken_sentence_count == 1, "destructor test request failed");
        // No explicit stop: destruction must wake and join the idle workers.
        assistant.reset();
    }
    require(player.completed() == std::vector<int>({new_marker, new_marker}),
            "backend reuse failed after worker destruction");
}

void cancelDuringTts() {
    Fixture f({}, {true});
    f.startOld();
    require(f.tts.old_entered.wait(), "TTS did not start");
    const auto interruption = f.assistant.stopPlayback();
    require(interruption.had_active_request && interruption.request_id == "old-request",
            "cancellation used wrong request");
    require(interruption.output_stopped, "local output did not stop");
    const auto old = take(f.old);
    require(!old.ok && old.spoken_sentence_count == 0, "interrupted TTS request succeeded");
    require(old.timing.queue_metrics_observed && !old.timing.queue_metrics_complete,
            "cancelled request falsely claims a complete queue snapshot");

    // Admit a new request while the previous synthesize() is still suspended.
    f.startNext();
    require(f.answer.new_query_entered.wait(), "new request not admitted before old TTS returned");
    f.tts.old_released.signal();
    const auto next = take(f.next);
    require(next.ok && next.spoken_sentence_count == 1, "new request failed after old synthesis");
    require(f.player.started() == std::vector<int>{new_marker}, "stale synthesized PCM reached play");
    require(f.player.completed() == std::vector<int>{new_marker}, "new PCM was lost");
    require(f.answer.cancellations() == std::vector<std::string>{"old-request"},
            "cancellation contaminated the next request");
}

void cancelDuringPlayback() {
    Fixture f({}, {}, {true});
    f.startOld();
    require(f.player.old_entered.wait(), "playback did not start");
    f.assistant.stopPlayback();
    require(!take(f.old).ok, "interrupted playback succeeded");
    f.checkNext();
    require(f.player.started() == std::vector<int>({old_marker, new_marker}),
            "unexpected playback sequence");
}

void sentenceBackpressure(bool shutdown) {
    // Build the options after TTS exists, without adding production test hooks.
    Tts tts({true});
    AnswerOptions options;
    options.old_sentence_count = 4;
    options.first_tts_entered = &tts.old_entered;
    Answer answer(options);
    Player player;
    VoiceAssistant assistant(answer, tts, player, answer, 1, 1);
    require(assistant.start(), "backpressure assistant did not start");
    std::future<VoiceAssistantResult> old;
    std::future<VoiceAssistantResult> next;
    auto cleanup = makeScopeExit([&] {
        tts.old_released.signal();
        answer.generation_released.signal();
        player.old_released.signal();
        assistant.stop();
        if (old.valid()) { old.wait(); }
        if (next.valid()) { next.wait(); }
    });
    old = std::async(std::launch::async, [&] {
        return assistant.processText("old-request", "101");
    });
    require(answer.third_chunk_entered.wait(), "sentence queue was not filled");
    // #1 is held in TTS, #2 occupies the only queue slot, #3 cannot finish
    // its callback until cancellation clears the queue or shutdown closes it.
    require(old.wait_for(0ms) != std::future_status::ready, "backpressured query already returned");
    if (shutdown) {
        // stop() joins the in-flight TTS, so release that mock first. The
        // capacity-one queue has already been established above.
        auto stopping = std::async(std::launch::async, [&] { assistant.stop(); });
        require(player.stopped.wait(), "stop did not reach local output");
        tts.old_released.signal();
        require(stopping.wait_for(deadline) == std::future_status::ready, "shutdown did not join TTS");
        stopping.get();
        require(!take(old).ok && !assistant.running(), "shutdown did not fail old request");
        require(player.started().empty(), "shutdown played old synthesized audio");
    } else {
        assistant.stopPlayback();
        tts.old_released.signal();
        require(!take(old).ok, "backpressured cancelled query succeeded");
        next = std::async(std::launch::async, [&] {
            return assistant.processText("new-request", "202");
        });
        require(take(next).ok, "new request failed after sentence backpressure");
        require(player.started() == std::vector<int>{new_marker}, "queued old sentence played");
    }
}

void audioBackpressure(bool shutdown) {
    AnswerOptions options;
    options.old_sentence_count = 4;
    Fixture f(options, {}, {true});
    f.startOld();
    require(f.player.old_entered.wait(), "old playback did not block");
    require(f.tts.third_entered.wait(), "TTS did not fill audio queue");
    // Playback holds #1, audio queue holds #2, TTS is producing #3.
    if (shutdown) {
        f.assistant.stop();
        require(!take(f.old).ok && !f.assistant.running(), "audio-backpressure shutdown failed");
        require(f.player.completed().empty(), "shutdown completed old playback");
    } else {
        f.assistant.stopPlayback();
        require(!take(f.old).ok, "audio-backpressure cancellation failed");
        f.checkNext();
        require(f.player.started() == std::vector<int>({old_marker, new_marker}),
                "queued stale PCM reached the player");
    }
}

void cancelGeneration() {
    AnswerOptions options;
    options.wait_for_cancel = true;
    Fixture f(options, {true});
    f.startOld();
    require(f.answer.generation_waiting.wait(), "generation did not wait for cancel");
    const auto result = f.assistant.stopPlayback();
    require(result.cancel_ok && result.generation_cancelled, "generation cancellation failed");
    require(!take(f.old).ok, "cancelled generation succeeded");
    f.tts.old_released.signal();
    f.checkNext();
}

void stopDuringTts() {
    Fixture f({}, {true});
    f.startOld();
    require(f.tts.old_entered.wait(), "TTS did not enter before stop");
    auto stopping = std::async(std::launch::async, [&] { f.assistant.stop(); });
    require(f.player.stopped.wait(), "stop did not mark request before TTS release");
    f.tts.old_released.signal();
    require(stopping.wait_for(deadline) == std::future_status::ready, "stop did not return");
    stopping.get();
    require(!take(f.old).ok && !f.assistant.running(), "stop did not terminate request");
    require(f.player.started().empty(), "stop during TTS emitted old audio");
    require(!f.assistant.start(), "closed workers unexpectedly restarted");
    f.assistant.stop();
}

void backendFault(Fault answer_fault, Fault tts_fault, Fault player_fault) {
    AnswerOptions a;
    a.answer_fault = answer_fault;
    Fixture f(a, {false, tts_fault}, {false, player_fault});
    f.startOld();
    const auto result = take(f.old);
    require(!result.ok && !result.error.empty(), "backend fault was not reported");
    f.checkNext();
}

void cancellationFault(Fault fault) {
    AnswerOptions options;
    options.cancel_fault = fault;
    Fixture f(options, {true});
    f.startOld();
    require(f.tts.old_entered.wait(), "TTS did not enter before failed cancel");
    const auto result = f.assistant.stopPlayback();
    require(result.output_stopped && result.cancel_attempted && !result.cancel_ok,
            "failed cancellation hid local stop or remote error");
    require(!result.error.empty(), "cancellation error missing");
    require(!take(f.old).ok, "failed remote cancellation resumed local output");
    f.tts.old_released.signal();
    f.checkNext();
}

void persistentMixedRequests() {
    AnswerOptions options;
    options.old_sentence_count = 0;
    options.wait_for_cancel = true;
    options.old_partial_before_wait = true;
    Fixture f(options);
    constexpr unsigned seed = 20261108;
    std::mt19937 random(seed);
    std::size_t normal = 0;
    std::size_t cancelled = 0;
    std::vector<std::string> cancellation_ids;
    for (std::size_t i = 0; i < 1000; ++i) {
        // Events are reset only after take() joined the preceding query.
        f.answer.generation_waiting.reset();
        f.answer.generation_released.reset();
        const bool interrupt = i == 1 || (i > 1 && random() % 3 == 0);
        const std::string id = "stress-request-" + std::to_string(i);
        f.old = std::async(std::launch::async, [&, id, interrupt] {
            return f.assistant.processText(id, interrupt ? "101" : "202");
        });
        if (interrupt) {
            require(f.answer.generation_waiting.wait(), id + ": generation not waiting");
            const auto stopped = f.assistant.stopPlayback();
            require(stopped.request_id == id && stopped.generation_cancelled,
                    id + ": cancellation routed to wrong request");
            cancellation_ids.push_back(id);
        }
        const auto result = take(f.old);
        require(result.request_id == id && result.ok != interrupt, id + ": wrong result");
        if (interrupt) { ++cancelled; } else { ++normal; }
        require(result.spoken_sentence_count == (interrupt ? 0U : 1U),
                id + ": wrong spoken count");
        require(f.player.completed().size() == normal, id + ": stale or missing PCM");
    }
    require(f.answer.requestCount() == 1000, "persistent request count mismatch");
    require(f.answer.cancellations() == cancellation_ids, "persistent cancellation IDs mismatch");
    for (int marker : f.player.completed()) {
        require(marker == new_marker, "partial old text contaminated normal speech");
    }
    f.assistant.stop();
    require(!f.assistant.running(), "persistent workers did not stop");
    std::cout << "[PERSISTENT] requests=1000 seed=" << seed << " normal=" << normal
              << " cancelled=" << cancelled << " unique_request_ids=1000 failures=0\n";
}

struct Scenario {
    const char* name;
    std::function<void()> run;
};
const std::array<Scenario, 18> scenarios{{
    {"normal_completion", normalCompletion},
    {"destructor_reclaims_idle_workers", destructorReclaimsWorkers},
    {"cancel_during_tts_and_start_next", cancelDuringTts},
    {"cancel_during_playback", cancelDuringPlayback},
    {"cancel_with_full_sentence_queue", [] { sentenceBackpressure(false); }},
    {"cancel_with_full_audio_queue", [] { audioBackpressure(false); }},
    {"cancel_blocked_generation", cancelGeneration},
    {"stop_during_tts", stopDuringTts},
    {"stop_with_full_sentence_queue", [] { sentenceBackpressure(true); }},
    {"stop_with_full_audio_queue", [] { audioBackpressure(true); }},
    {"answer_failure_then_recover", [] { backendFault(Fault::Failure, Fault::None, Fault::None); }},
    {"answer_exception_then_recover", [] { backendFault(Fault::Exception, Fault::None, Fault::None); }},
    {"tts_failure_then_recover", [] { backendFault(Fault::None, Fault::Failure, Fault::None); }},
    {"tts_exception_then_recover", [] { backendFault(Fault::None, Fault::Exception, Fault::None); }},
    {"playback_failure_then_recover", [] { backendFault(Fault::None, Fault::None, Fault::Failure); }},
    {"playback_exception_then_recover", [] { backendFault(Fault::None, Fault::None, Fault::Exception); }},
    {"cancel_failure_keeps_local_output_stopped", [] { cancellationFault(Fault::Failure); }},
    {"cancel_exception_keeps_local_output_stopped", [] { cancellationFault(Fault::Exception); }},
}};
}  // namespace

int main(int argc, char* argv[]) {
    const bool stress = argc == 2 && std::string(argv[1]) == "--stress";
    if (argc != 1 && !stress) {
        std::cerr << "Usage: voice_lifecycle_tests [--stress]\n";
        return 2;
    }
    constexpr unsigned seed = 20261108;
    const std::size_t rounds = stress ? 1000 : scenarios.size();
    std::mt19937 random(seed);
    std::array<std::size_t, scenarios.size()> counts{};
    for (std::size_t i = 0; i < rounds; ++i) {
        const std::size_t index = i < scenarios.size() ? i : random() % scenarios.size();
        const auto& scenario = scenarios[index];
        try {
            scenario.run();
            ++counts[index];
            if (!stress) { std::cout << "[PASS] " << scenario.name << '\n'; }
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] iteration=" << i << " seed=" << seed
                      << " scenario=" << scenario.name << ": " << e.what() << '\n';
            return 1;
        }
    }
    if (stress) {
        try { persistentMixedRequests(); }
        catch (const std::exception& e) {
            std::cerr << "[FAIL] persistent_requests: " << e.what() << '\n';
            return 1;
        }
    }
    std::cout << "[SUMMARY] scenarios=" << rounds << " seed=" << seed << " failures=0\n";
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        std::cout << "  " << scenarios[i].name << '=' << counts[i] << '\n';
    }
    return 0;
}
