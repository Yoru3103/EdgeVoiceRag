#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <sched.h>
#include <sys/resource.h>
#include <nlohmann/json.hpp>

#include "board_app_config.h"
#include "board_tts_config.h"
#include "file_fingerprint.h"
#include "sherpa_onnx_tts_backend.h"

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Options {
    std::string config = "config/board_rk3588.conf";
    std::string cases = "benchmarks/tts_cases.json";
    std::string text, model_dir, model_file;
    int threads = 0, speaker = -1, warmup = 1, runs = 5;
    bool help = false, cases_given = false, text_given = false;
};

void usage() {
    std::cout << "tts_benchmark [--config PATH] [--cases JSON | --text TEXT]\n"
              << "  [--model-dir DIR] [--model-file NAME] [--speaker-id N]\n"
              << "  [--threads N] [--warmup N] [--runs N]\n"
              << "Synthesis only; no ALSA, ASR, VAD, retrieval or LLM. JSONL v1.\n";
}

int number(const std::string& text, int low, int high) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected an unsigned integer");
    std::size_t used = 0;
    const int n = std::stoi(text, &used);
    if (used != text.size() || n < low || n > high)
        throw std::invalid_argument("integer out of range");
    return n;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help" || key == "-h") { o.help = true; continue; }
        if (key != "--config" && key != "--cases" && key != "--text"
            && key != "--model-dir" && key != "--model-file" && key != "--speaker-id"
            && key != "--threads" && key != "--warmup" && key != "--runs")
            throw std::invalid_argument("unknown option: " + key);
        if (++i >= argc) throw std::invalid_argument("missing value for " + key);
        const std::string value = argv[i];
        if (value.empty() || value.rfind("--", 0) == 0)
            throw std::invalid_argument("missing value for " + key);
        if (key == "--config") o.config = value;
        else if (key == "--cases") { o.cases = value; o.cases_given = true; }
        else if (key == "--text") { o.text = value; o.text_given = true; }
        else if (key == "--model-dir") o.model_dir = value;
        else if (key == "--model-file") o.model_file = value;
        else if (key == "--speaker-id") o.speaker = number(value, 0, 100000);
        else if (key == "--threads") o.threads = number(value, 1, 32);
        else if (key == "--warmup") o.warmup = number(value, 0, 20);
        else o.runs = number(value, 1, 1000);
    }
    if (o.cases_given && o.text_given)
        throw std::invalid_argument("--cases and --text are mutually exclusive");
    if (o.text.size() > 4096) throw std::invalid_argument("text exceeds 4096 UTF-8 bytes");
    return o;
}

struct Case { std::string id, text; };

std::vector<Case> loadCases(const Options& o) {
    if (o.text_given) return {{"inline", o.text}};
    std::ifstream file(o.cases);
    if (!file) throw std::runtime_error("cannot read cases: " + o.cases);
    Json value; file >> value;
    if (value.at("version") != 1 || !value.at("cases").is_array()
        || value["cases"].empty() || value["cases"].size() > 100)
        throw std::runtime_error("invalid benchmark case list");
    std::vector<Case> result;
    std::set<std::string> ids;
    for (const auto& item : value["cases"]) {
        Case c{item.at("id").get<std::string>(), item.at("text").get<std::string>()};
        if (c.id.empty() || c.id.size() > 128 || c.text.empty() || c.text.size() > 4096
            || c.text.find_first_not_of(" \t\r\n") == std::string::npos
            || !ids.insert(c.id).second)
            throw std::runtime_error("empty/duplicate/oversized benchmark case");
        result.push_back(std::move(c));
    }
    return result;
}

void emit(Json value) { value["version"] = 1; std::cout << value.dump() << std::endl; }
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::string fingerprint(const std::string& path) {
    const auto result = calculateFileFnv1a64(path);
    if (!result.ok) throw std::runtime_error(result.error);
    return result.fingerprint;
}
Json peakRssKb() {
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) return nullptr;
    return usage.ru_maxrss; // Linux: KiB, process lifetime peak, not per-call allocation
}
Json allowedCpus() {
    cpu_set_t mask; CPU_ZERO(&mask);
    if (::sched_getaffinity(0, sizeof mask, &mask) != 0) return nullptr;
    Json cpus = Json::array();
    for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &mask)) cpus.push_back(i);
    return cpus;
}
// Type-7 linear interpolation, matching the documented performance guide.
double percentile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    const double position = (values.size() - 1) * q;
    const auto low = static_cast<std::size_t>(position);
    const auto high = std::min(low + 1, values.size() - 1);
    return values[low] + (values[high] - values[low]) * (position - low);
}
} // namespace

int main(int argc, char** argv) {
    Options options;
    try { options = parse(argc, argv); }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; usage(); return 2;
    }
    if (options.help) { usage(); return 0; }
    try {
        auto config = BoardAppConfig::load(options.config);
        if (!options.model_dir.empty()) config.tts_model_dir = options.model_dir;
        if (!options.model_file.empty()) config.tts_model_file = options.model_file;
        if (options.threads) config.tts_num_threads = options.threads;
        if (options.speaker >= 0) config.tts_speaker_id = options.speaker;
        config.validate();
        const auto cases = loadCases(options);
        const auto tts_config = makeBoardTtsConfig(config);
        Json inputs = {{"model", fingerprint(tts_config.model_path)},
                       {"lexicon", fingerprint(tts_config.lexicon_path)},
                       {"tokens", fingerprint(tts_config.tokens_path)}};
        Json rules = Json::array();
        for (const auto& path : tts_config.rule_fsts)
            rules.push_back({{"path", path}, {"fnv1a64", fingerprint(path)}});
        auto start = Clock::now();
        SherpaOnnxTtsBackend tts(tts_config);
        const double load_ms = elapsed(start);
        emit({{"type", "tts_benchmark_config"}, {"config", options.config},
              {"model_path", tts_config.model_path}, {"num_threads", tts_config.num_threads},
              {"speaker_id", tts_config.speaker_id}, {"speed", tts_config.speed},
              {"provider", tts_config.provider}, {"allowed_cpus", allowedCpus()},
              {"model_load_ms", load_ms}, {"process_peak_rss_kb_after_load", peakRssKb()}, {"warmup_per_case", options.warmup},
              {"runs_per_case", options.runs}, {"case_count", cases.size()},
              {"input_fnv1a64", inputs}, {"rule_fsts", rules},
              {"cases_source", options.text_given ? "inline" : options.cases},
              {"cases_fnv1a64", options.text_given ? Json(nullptr) : Json(fingerprint(options.cases))}});
        bool first = true;
        for (const auto& c : cases) {
            std::vector<double> times, rtfs;
            for (int i = 0; i < options.warmup + options.runs; ++i) {
                const bool warmup = i < options.warmup;
                start = Clock::now();
                const auto result = tts.synthesize(c.text);
                const double ms = elapsed(start);
                const bool valid = result.ok && !result.audio.empty()
                    && result.audio.durationSeconds() > 0.0;
                Json sample = {{"type", "tts_benchmark_sample"}, {"case_id", c.id},
                    {"text", c.text}, {"text_utf8_bytes", c.text.size()},
                    {"phase", warmup ? "warmup" : "measured"},
                    {"iteration", warmup ? i + 1 : i - options.warmup + 1},
                    {"first_inference", first}, {"ok", valid},
                    {"process_peak_rss_kb", peakRssKb()},
                    {"synthesis_ms", ms}, {"error", result.error}};
                first = false;
                if (!valid) { emit(sample); return 1; }
                const double duration = result.audio.durationSeconds();
                const double rtf = ms / (duration * 1000.0);
                sample.update({{"sample_rate", result.audio.sample_rate},
                               {"channels", result.audio.channels},
                               {"frames", result.audio.frameCount()},
                               {"audio_seconds", duration}, {"rtf", rtf}});
                emit(sample);
                if (!warmup) { times.push_back(ms); rtfs.push_back(rtf); }
            }
            emit({{"type", "tts_benchmark_summary"}, {"case_id", c.id},
                  {"samples", times.size()}, {"warmup_excluded", options.warmup},
                  {"synthesis_p50_ms", percentile(times, 0.5)},
                  {"synthesis_p95_ms", percentile(times, 0.95)},
                  {"rtf_p50", percentile(rtfs, 0.5)}, {"rtf_p95", percentile(rtfs, 0.95)},
                  {"percentile_method", "type7_linear"}});
        }
        return 0;
    } catch (const std::exception& e) {
        emit({{"type", "tts_benchmark_error"}, {"error", e.what()}});
        return 1;
    }
}
