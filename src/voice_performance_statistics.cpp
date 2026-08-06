#include "voice_performance_statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

const std::string KPerformancePrefix = "[PERF_JSON] ";

constexpr int KPerformanceVersion = 1;

constexpr const char* KPerformanceType = "voice_turn_performance";

double requireNonNegativeNumber(
    const Json& report,
    const std::string& field
) {
    if (!report.contains(field) || !report.at(field).is_number()) {
        throw std::runtime_error(
            "missing or invalid numeric field: "
            + field
        );
    }

    const double value = report.at(field).get<double>();

    if (!std::isfinite(value) || value < 0.0) {
        throw std::runtime_error(
            "performance field must be finite "
            "and non-negative: "
            + field
        );
    }

    return value;
}

bool requireBoolean(
    const Json& report,
    const std::string& field
) {
    if (
        !report.contains(field)
        || !report.at(field).is_boolean()
    ) {
        throw std::runtime_error(
            "missing or invalid boolean field: "
            + field
        );
    }

    return report.at(field).get<bool>();
}

void validateHeader(const Json& report) {
    if (!report.is_object()) {
        throw std::runtime_error(
            "performance report must be a JSON object"
        );
    }

    if (
        !report.contains("version")
        || !report.at("version").is_number_integer()
        || report.at("version").get<int>() != KPerformanceVersion
    ) {
        throw std::runtime_error(
            "unsupported performance report version"
        );
    }

    if (
        !report.contains("type")
        || !report.at("type").is_string()
        || report.at("type").get<std::string>()
            != KPerformanceType
    ) {
        throw std::runtime_error(
            "unsupported performance report type"
        );
    }

     if (
        !report.contains("request_id")
        || !report.at("request_id").is_string()
        || report.at("request_id")
               .get<std::string>()
               .empty()
    ) {
        throw std::runtime_error(
            "performance request_id must not be empty"
        );
    }
}

}   // namespace

bool VoicePerformanceAggregator::addLogLine(
    const std::string& line,
    std::string& error
) {
    input_line_count_++;
    error.clear();

    if (line.compare(0, KPerformancePrefix.size(), KPerformancePrefix) != 0) {
        ignored_line_count_++;
        return false;
    }

    try {
        const std::string payload = line.substr(KPerformancePrefix.size());

        const Json report = Json::parse(payload);

        validateHeader(report);

        const bool ok = requireBoolean(report, "ok");

        /*
         * 先把当前报告的数据放进局部 map。
         * 只有整个报告全部校验通过后，才写入成员变量。
         *
         * 避免前几个字段已经插入，但后面的字段校验失败，
         * 造成半条报告污染统计结果。
         */
        std::map<std::string, double> values;

        const char* always_recorded_fields[] = {
            "capture_elapsed_ms",
            "asr_elapsed_ms",
            "answer_backend_elapsed_ms",
            "retrieval_elapsed_ms",
            "llm_elapsed_ms",
            "tts_elapsed_ms",
            "playback_elapsed_ms",
            "assistant_total_elapsed_ms",
            "turn_elapsed_ms"
        };

        for (const char* field : always_recorded_fields) {
            values.emplace(field, requireNonNegativeNumber(report, field));
        }

        /*
         * 首 Token、首文本等指标只有 observed=true
         * 时才有意义。
         *
         * observed=false 时 JSON 中的时间通常是 0，
         * 不能把这个 0 放进 P50/P95，否则会污染结果。
         */
        const double llm_ttft = requireNonNegativeNumber(report, "llm_time_to_first_token_ms");
        if (requireBoolean(report, "llm_first_token_observed")) {
            values.emplace("llm_time_to_first_token_ms", llm_ttft);
        }

        const double first_answer_text = requireNonNegativeNumber(report, "first_answer_text_ms");
        if (requireBoolean(report, "first_answer_text_observed")) {
            values.emplace("first_answer_text_ms", first_answer_text);
        }

        const double first_audio_ready = requireNonNegativeNumber(report, "first_audio_ready_ms");
        if (requireBoolean(report, "first_audio_ready_observed")) {
            values.emplace("first_audio_ready_ms", first_audio_ready);
        }

        const double first_playback_start  = requireNonNegativeNumber(report, "first_playback_start_ms");
        if (requireBoolean(report, "first_playback_started")) {
            values.emplace("first_playback_start_ms", first_playback_start);
        }

        // 无异常时再统一写入
        for (const auto& item : values) {
            metric_values_[item.first].push_back(item.second);
        }

        valid_report_count_++;

        if (ok) {
            successful_report_count_++;
        }

        return true;
    } catch (const std::exception& exception) {
        invalid_report_count_++;
        error = exception.what();

        return false;
    }
}

double VoicePerformanceAggregator::percentile(
    const std::vector<double>& sorted_values,
    double ratio
) {
    if (sorted_values.empty()) {
        return 0.0;
    }

    if (ratio <= 0.0) {
        return sorted_values.front();
    }

    if (ratio >= 1.0) {
        return sorted_values.back();
    }

    /*
     * 使用线性插值：
     *
     * 位置 = (样本数 - 1) × 百分位
     *
     * 例如 [10, 20, 30, 40] 的 P50：
     * position = 3 × 0.5 = 1.5
     * result   = 20 + 0.5 × (30 - 20) = 25
     */
    const double position = static_cast<double>(sorted_values.size() - 1) * ratio;
    const std::size_t lower_index = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper_index = static_cast<std::size_t>(std::ceil(position));

    if (lower_index == upper_index) {
        return sorted_values[lower_index];
    }

    const double fraction = position - static_cast<double>(lower_index);

    return sorted_values[lower_index] + fraction * (sorted_values[upper_index] - sorted_values[lower_index]);
}

VoicePerformanceStatistics VoicePerformanceAggregator::summarize() const {
    VoicePerformanceStatistics result;

    result.input_line_count = input_line_count_;
    result.ignored_line_count = ignored_line_count_;
    result.valid_report_count = valid_report_count_;
    result.invalid_report_count = invalid_report_count_;
    result.successful_report_count = successful_report_count_;

    if (valid_report_count_ > 0) {
        result.reported_success_rate = 100.0 * static_cast<double>(successful_report_count_) / static_cast<double>(valid_report_count_);
    }

    for (const auto& metric : metric_values_) {
        std::vector<double> sorted_values = metric.second;

        std::sort(sorted_values.begin(), sorted_values.end());

        VoiceMetricStatistics statistics;

        statistics.sample_count = sorted_values.size();

        if (sorted_values.empty()) {
            result.metrics.emplace(metric.first, statistics);

            continue;
        }

        const double total = std::accumulate(sorted_values.begin(), sorted_values.end(), 0.0);

        statistics.average_ms = total / static_cast<double>(sorted_values.size());
        statistics.p50_ms = percentile(sorted_values, 0.50);
        statistics.p95_ms = percentile(sorted_values, 0.95);
        
        statistics.maximum_ms = sorted_values.back();

        result.metrics.emplace(
            metric.first,
            statistics
        );
    }

    return result;
}
