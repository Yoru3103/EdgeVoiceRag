#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

struct VoiceMetricStatistics {
    std::size_t sample_count = 0;

    double average_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double maximum_ms = 0.0;
};

struct VoicePerformanceStatistics {
    // 输入文件中的总行数。
    std::size_t input_line_count = 0;

    // 非 PERF_JSON 日志，例如 [VOICE]、[ASR]。
    std::size_t ignored_line_count = 0;

    // 通过 schema 校验的性能报告数。
    std::size_t valid_report_count = 0;

    // 以 PERF_JSON 开头，但 JSON 无效的报告数。
    std::size_t invalid_report_count = 0;

    // valid_report 中 ok=true 的数量。
    std::size_t successful_report_count = 0;

    // 这里只表示 PERF_JSON 报告内部的成功率。
    double reported_success_rate = 0.0;

    std::map<std::string, VoiceMetricStatistics> metrics;
};

class VoicePerformanceAggregator {
public:
    /*
     * 返回 true：成功接受一条 PERF_JSON。
     *
     * 返回 false 且 error 为空：
     * 普通日志行，被忽略。
     *
     * 返回 false 且 error 非空：
     * PERF_JSON 行存在格式或字段错误。
     */
    bool addLogLine(
        const std::string& line,
        std::string& error
    );

    VoicePerformanceStatistics summarize() const;

private:
    std::size_t input_line_count_ = 0;
    std::size_t ignored_line_count_ = 0;
    std::size_t valid_report_count_ = 0;
    std::size_t invalid_report_count_ = 0;
    std::size_t successful_report_count_ = 0;

    std::map<std::string, std::vector<double>> metric_values_;

    static double percentile(
        const std::vector<double>& sorted_values,
        double ratio
    );
};
