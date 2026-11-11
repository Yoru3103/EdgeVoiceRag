#include <cmath>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "voice_performance_statistics.h"

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

bool nearlyEqual(
    double left,
    double right,
    double tolerance = 0.000001
) {
    return std::abs(left - right) <= tolerance;
}

std::string makePerformanceLine(
    int sequence,
    double value,
    bool ok,
    bool first_token_observed
) {
    const Json report = {
        {"version", 1},
        {"type", "voice_turn_performance"},
        {
            "request_id",
            "voice-session"
                + std::to_string(sequence)
        },
        {"ok", ok},

        {"capture_elapsed_ms", value},
        {"asr_elapsed_ms", value},
        {"answer_backend_elapsed_ms", value},
        {"retrieval_elapsed_ms", value},
        {"llm_elapsed_ms", value},
        {"tts_elapsed_ms", value},
        {"playback_elapsed_ms", value},
        {"assistant_total_elapsed_ms", value},
        {"turn_elapsed_ms", value},

        {
            "llm_first_token_observed",
            first_token_observed
        },
        {
            "llm_time_to_first_token_ms",
            value
        },

        {"first_answer_text_observed", true},
        {"first_answer_text_ms", value},

        {"first_audio_ready_observed", true},
        {"first_audio_ready_ms", value},

        {"first_playback_started", true},
        {"first_playback_start_ms", value}
    };

    return "[PERF_JSON] " + report.dump();
}

void testAggregatePerformanceReports() {
    VoicePerformanceAggregator aggregator;

    std::string error;

    expectTrue(
        !aggregator.addLogLine(
            "[VOICE] waiting",
            error
        ) && error.empty(),
        "ignore ordinary log line"
    );

    expectTrue(
        aggregator.addLogLine(
            makePerformanceLine(
                1,
                10.0,
                true,
                false
            ),
            error
        ),
        "accept first report"
    );

    expectTrue(
        aggregator.addLogLine(
            makePerformanceLine(
                2,
                20.0,
                true,
                true
            ),
            error
        ),
        "accept second report"
    );

    expectTrue(
        aggregator.addLogLine(
            makePerformanceLine(
                3,
                30.0,
                false,
                true
            ),
            error
        ),
        "accept failed report"
    );

    expectTrue(
        aggregator.addLogLine(
            makePerformanceLine(
                4,
                40.0,
                true,
                true
            ),
            error
        ),
        "accept fourth report"
    );

    expectTrue(
        !aggregator.addLogLine(
            "[PERF_JSON] {invalid",
            error
        ) && !error.empty(),
        "reject invalid JSON report"
    );

    const VoicePerformanceStatistics result =
        aggregator.summarize();

    expectTrue(
        result.input_line_count == 6,
        "count all input lines"
    );

    expectTrue(
        result.ignored_line_count == 1,
        "count ignored lines"
    );

    expectTrue(
        result.valid_report_count == 4,
        "count valid reports"
    );

    expectTrue(
        result.invalid_report_count == 1,
        "count invalid reports"
    );

    expectTrue(
        result.successful_report_count == 3,
        "count successful reports"
    );

    expectTrue(
        nearlyEqual(
            result.reported_success_rate,
            75.0
        ),
        "calculate reported success rate"
    );

    const VoiceMetricStatistics& turn =
        result.metrics.at("turn_elapsed_ms");

    expectTrue(
        turn.sample_count == 4,
        "count turn samples"
    );

    expectTrue(
        nearlyEqual(turn.average_ms, 25.0),
        "calculate average"
    );

    expectTrue(
        nearlyEqual(turn.p50_ms, 25.0),
        "calculate interpolated P50"
    );

    expectTrue(
        nearlyEqual(turn.p95_ms, 38.5),
        "calculate interpolated P95"
    );

    expectTrue(
        nearlyEqual(turn.maximum_ms, 40.0),
        "calculate maximum"
    );

    /*
     * 第一条报告 first_token_observed=false，
     * 因此 TTFT 只有三个有效样本。
     */
    const VoiceMetricStatistics& ttft =
        result.metrics.at(
            "llm_time_to_first_token_ms"
        );

    expectTrue(
        ttft.sample_count == 3,
        "exclude unobserved TTFT"
    );

    expectTrue(
        nearlyEqual(ttft.p50_ms, 30.0),
        "calculate conditional metric P50"
    );
}

}  // namespace

void testOptionalQueueMetrics() {
    auto report = Json::parse(makePerformanceLine(10, 10.0, true, true).substr(12));
    report["queue_metrics_observed"] = true;
    report["queue_metrics_complete"] = true;
    for (const std::string prefix : {"sentence_queue", "audio_queue"}) {
        report[prefix + "_capacity"] = 2;
        report[prefix + "_peak_depth"] = 1;
        report[prefix + "_push_wait_count"] = 0;
        report[prefix + "_push_wait_ms"] = 0.0;
    }
    VoicePerformanceAggregator a;
    std::string error;
    expectTrue(a.addLogLine("[PERF_JSON] " + report.dump(), error), "accept complete zero-wait observation");
    auto partial = report;
    partial["queue_metrics_complete"] = false;
    partial["sentence_queue_push_wait_ms"] = 99.0;
    expectTrue(a.addLogLine("[PERF_JSON] " + partial.dump(), error), "accept partial snapshot without aggregation");
    auto invalid = report;
    invalid["audio_queue_peak_depth"] = 3;
    expectTrue(!a.addLogLine("[PERF_JSON] " + invalid.dump(), error), "reject peak greater than capacity");
    invalid = report;
    invalid.erase("audio_queue_push_wait_count");
    expectTrue(!a.addLogLine("[PERF_JSON] " + invalid.dump(), error), "reject half-present optional group");
    invalid = report;
    invalid["audio_queue_push_wait_count"] = 0.5;
    expectTrue(!a.addLogLine("[PERF_JSON] " + invalid.dump(), error), "reject fractional queue count");
    const auto result = a.summarize();
    expectTrue(result.valid_report_count == 2 && result.invalid_report_count == 3,
               "optional group validation is atomic");
    const auto metric = result.metrics.at("sentence_queue_push_wait_ms");
    expectTrue(metric.sample_count == 1 && metric.average_ms == 0.0,
               "observed zero retained; partial snapshot excluded");
}

int main() {
    testAggregatePerformanceReports();
    testOptionalQueueMetrics();

    if (failed_count == 0) {
        std::cout
            << "\nAll voice performance statistics "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
