#pragma once

#include <string>

enum class LogLevel {
    Info,
    Warning,
    Error,
    User,
    Route,
    System,
    Perf
};

class Logger {  //没有普通成员变量，可以全部使用static，这样以后使用时不需要创建对象
public:
    static void log(LogLevel level, const std::string& message);

private:
    static std::string levelToString(LogLevel level);
};