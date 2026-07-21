// ZeroMQ REP 网络服务
#include <atomic>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>

#include <zmq.hpp>

#include "llm_service.h"
#include "llm_backend_factory.h"

namespace {

std::atomic_bool g_running{true};

void handleSignal(int signal_number) {
    (void)signal_number;
    g_running.store(false);
}

struct ServerOptions {
    std::string endpoint = "tcp://*:8899";
    std::string backend = "mock";
    std::string model_path;
    int max_new_tokens = 512;
    int max_context_len = 4096;
    bool show_help = false;
};

std::string usage(const std::string& program_name) {
    return
        "Usage:\n"
        "  " + program_name + " [options]\n\n"
        "Options:\n"
        "  --endpoint <endpoint>          Bind endpoint\n"
        "  --backend <mock|rkllm>         LLM backend\n"
        "  --model <path>                 RKLLM model path\n"
        "  --max-new-tokens <number>      Maximum generated tokens\n"
        "  --max-context-len <number>     Maximum context length\n"
        "  -h, --help                     Show help\n";
}

ServerOptions parseOptions(int argc, char* argv[]) {
    ServerOptions options;

    for (int index = 1; index < argc; index++) {
        const std::string argument = argv[index];

        if (argument == "-h" || argument == "--help") {
            options.show_help = true;
            return options;
        }

        if (argument == "--endpoint") {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after --endpoint");
            }

            options.endpoint = argv[++index];
            continue;
        }

        if (argument == "--backend") {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after --backend");
            }

            options.backend = argv[++index];
            continue;
        }

        if (argument == "--model") {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after --model");
            }

            options.model_path = argv[++index];
            continue;
        }

        if (argument == "--max-new-tokens") {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after --max-new-tokens");
            }

            options.max_new_tokens = std::stoi(argv[++index]);
            continue;
        }

        if (argument == "--max-context-len") {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value after --max-context-len");
            }

            options.max_context_len = std::stoi(argv[++index]);
            continue;
        }

        throw std::runtime_error(
            "unknown argument: " + argument
        );
    }

    if (options.max_new_tokens <= 0) {
        throw std::runtime_error("--max-new-tokens must be greater than zero");
    }

    if (options.max_context_len <= 0) {
        throw std::runtime_error("--max-context-len must be greater than zero");
    }

    if (options.backend == "rkllm" && options.model_path.empty()) {
        throw std::runtime_error("--model is required when backend is rkllm");
    }

    return options;
}

std::string messageToString(const zmq::message_t& message) {
    return std::string(
        static_cast<const char*>(message.data()), message.size()
    );
}
}   // namespace

int main(int argc, char* argv[]) {
    const std::string program_name = argc > 0 ? argv[0] : "llm_server";

    ServerOptions options;

    try {
        options = parseOptions(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] " << error.what() << '\n';
        std::cerr << usage(program_name);
        return 1;
    }

    if (options.show_help) {
        std::cout << usage(program_name);
        return 0;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        LlmBackendOptions backend_options;
        backend_options.backend = options.backend;
        backend_options.max_context_len = options.max_context_len;
        backend_options.max_new_tokens = options.max_new_tokens;
        backend_options.model_path = options.model_path;

        std::unique_ptr<LlmBackend> backend = createLlmBackend(backend_options);

        LlmService service(*backend);

        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::rep);

        socket.set(zmq::sockopt::linger, 0);
        socket.set(zmq::sockopt::rcvtimeo, 500);
        socket.bind(options.endpoint);

        std::cout << "[INFO] C++ LLM server started\n";
        std::cout << "[INFO] Endpoint: " << options.endpoint << '\n';
        std::cout << "[INFO] Backend: " << backend->name() << '\n';

        while (g_running.load()) {
            zmq::message_t request_message;

            try {
                const auto received = socket.recv(
                    request_message,
                    zmq::recv_flags::none
                );

                if (!received.has_value()) {
                    continue;
                }

                const std::string request = messageToString(request_message);

                const std::string response = service.handleMessage(request);

                const auto sent = socket.send(
                    zmq::buffer(response),
                    zmq::send_flags::none
                );

                if (!sent.has_value()) {
                    std::cerr
                        << "[WARNING] Failed to send LLM response\n";
                }
            } catch (const zmq::error_t& error) {
                if (error.num() == EAGAIN || error.num() == EINTR) {
                    continue;
                }

                throw;
            }
        }

        socket.close();
        context.close();

        std::cout << "[INFO] C++ LLM server stopped\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "[ERROR] LLM server failed: "
            << error.what()
            << '\n';

        return 1;
    }
}