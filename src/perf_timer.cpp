#include "perf_timer.h"

PerfTimer::PerfTimer(const std::string& name) 
    : name_(name)
    , start_time_(std::chrono::steady_clock::now()) {
}

void PerfTimer::reset() {
    start_time_ = std::chrono::steady_clock::now();
}

double PerfTimer::elapsedMilliseconds() const{
    auto end_time = std::chrono::steady_clock::now();

    // <> 在 C++ 里通常表示 模板参数列表
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time_
    );

    return duration.count() / 1000.0;
}

const std::string& PerfTimer::name() const {
    return name_;
}