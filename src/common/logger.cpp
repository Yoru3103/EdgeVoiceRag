#include "logger.h"

#include <iostream>

void Logger::log(LogLevel level, const std::string& message) {
    std::cout << "[" << levelToString(level) << "] " << message << std::endl;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::User:
            return "USER";
        case LogLevel::Route:
            return "ROUTE";
        case LogLevel::System:
            return "SYSTEM";
        case LogLevel::Perf:
            return "PERF";
        default:
            return "UNKNOWN";
    }
}