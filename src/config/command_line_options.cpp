#include "command_line_options.h"

#include <sstream>

CommandLineOptions CommandLineOptions::parse(int argc, char* argv[]) {
    CommandLineOptions options;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            options.show_help_ = true;
            return options;
        }

        if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) {
                options.has_error_ = true;
                options.error_message_ = "Missing value after " + arg;
                return options;
            }

            options.config_path_ = argv[++i];
            continue;
        }

        if (arg == "--once") {
            if (i + 1 >= argc) {
                options.has_error_ = true;
                options.error_message_ = "Missing query after --once";
                return options;
            }

            options.once_mode_ = true;
            options.once_query_ = argv[++i];

            if (options.once_query_.empty()) {
                options.has_error_ = true;
                options.error_message_ = "Query after --once cannot be empty";
                return options;
            }

            continue;
        }

        options.has_error_ = true;
        options.error_message_ = "Unknown argument: " + arg;
        return options;
    }

    return options;
}

const std::string& CommandLineOptions::configPath() const {
    return config_path_;
}

bool CommandLineOptions::showHelp() const {
    return show_help_;
}

bool CommandLineOptions::hasError() const {
    return has_error_;
}

const std::string& CommandLineOptions::errorMessage() const {
    return error_message_;
}

bool CommandLineOptions::onceMode() const {
    return once_mode_;
}

const std::string& CommandLineOptions::onceQuery() const {
    return once_query_;
}

std::string CommandLineOptions::usage(const std::string& program_name) {
    std::ostringstream oss;

    oss << "Usage:\n"
        << "  " << program_name << " [options]\n\n"
        << "Options:\n"
        << "  -c, --config <path>    Specify config file path. Default: config/app.conf\n"
        << "  --once <query>         Run one query and exit\n"
        << "  -h, --help             Show this help message\n\n"
        << "Examples:\n"
        << "  " << program_name << "\n"
        << "  " << program_name << " --config config/dev.conf\n"
        << "  " << program_name << " -c config/docker.conf\n"
        << "  " << program_name << " --once \"空调怎么打开\"\n"
        << "  " << program_name << " -c config/dev.conf --once \"蓝牙怎么连接\"\n";

    return oss.str();
}