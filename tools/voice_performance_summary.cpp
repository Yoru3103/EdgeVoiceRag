#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "voice_performance_statistics.h"

namespace {

using Json = nlohmann::json;

void processInput(std::istream& input, VoicePerformanceAggregator& aggregator) {
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        line_number++;

        std::string error;

        const bool accepted = aggregator.addLogLine(line, error);

        if (!accepted && !error.empty()) {
            std::cerr
                << "[WARNING] invalid PERF_JSON "
                << "at line "
                << line_number
                << ": "
                << error
                << '\n';
        }
    }
}

Json encodeStatistics(const VoicePerformanceStatistics& statistics) {
    Json metrics = Json::object();

    for (const auto& item : statistics.metrics) {
        const VoiceMetricStatistics& value = item.second;

        metrics[item.first] = {
            {"sample_count", value.sample_count},
            {"average_ms", value.average_ms},
            {"p50_ms", value.p50_ms},
            {"p95_ms", value.p95_ms},
            {"maximum_ms", value.maximum_ms}
        };
    }

    return Json{
        {"version", 1},
        {"type", "voice_performance_summary"},
        {"input_line_count", statistics.input_line_count},
        {"ignored_line_count", statistics.ignored_line_count},
        {"valid_report_count", statistics.valid_report_count},
        {"invalid_report_count", statistics.invalid_report_count},
        {"successful_report_count", statistics.successful_report_count},
        {"reported_success_rate", statistics.reported_success_rate},
        {"metrics", std::move(metrics)}
    };
}

}   // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 2) {
            throw std::invalid_argument(
                "usage: voice_performance_summary "
                "[log_path]"
            );
        }

        VoicePerformanceAggregator aggregator;

        if (argc == 2) {
            const std::string input_path = argv[1];

            std::ifstream input(input_path);

            if (!input) {
                throw std::runtime_error(
                    "failed to open performance log: "
                    + input_path
                );
            }

            processInput(input, aggregator);
        } else {
            processInput(
                std::cin,
                aggregator
            );
        }

        const VoicePerformanceStatistics statistics = aggregator.summarize();

        if (statistics.valid_report_count == 0) {
            std::cerr << "no valid PERF_JSON reports found\n";

            return 2;
        }

        const Json summary = encodeStatistics(statistics);

        std::cout
            << std::fixed
            << std::setprecision(3)
            << summary.dump(2)
            << '\n';

        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "Voice performance summary failed: "
            << exception.what()
            << '\n';

        return 1;
    }
}
