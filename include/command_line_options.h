#pragma once

#include <string>

class CommandLineOptions {
public:
    static CommandLineOptions parse(int argc, char* argv[]);

    const std::string& configPath() const;
    bool showHelp() const;
    bool hasError() const;
    const std::string& errorMessage() const;

    bool onceMode() const;
    const std::string& onceQuery() const;

    static std::string usage(const std::string& program_name);

private:
    std::string config_path_ = "config/app.conf";
    bool show_help_ = false;
    bool has_error_ = false;
    std::string error_message_;

    bool once_mode_ = false;
    std::string once_query_;
};