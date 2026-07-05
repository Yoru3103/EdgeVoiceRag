#pragma once

#include <chrono>
#include <string>

class PerfTimer {
public:
    explicit PerfTimer(const std::string& name);

    void reset();

    double elapsedMilliseconds() const;

    const std::string& name() const;

private:
    std::string name_;
    //单调递增的时钟,适合做性能计算。system_clock表示系统真实时间，可能会因为系统校准而跳变
    std::chrono::steady_clock::time_point start_time_;  
};