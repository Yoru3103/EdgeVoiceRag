// ZeroMQ REP 网络服务
#include <array>
#include <atomic>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>

#include <zmq.hpp>

#include "llm_service.h"
#include "llm_backend_factory.h"
#include "llm_stream_service.h"

namespace {

std::atomic_bool g_running{true};

void handleSignal(int signal_number) {
    (void)signal_number;
    g_running.store(false);
}

struct ServerOptions {
    std::string endpoint = "tcp://*:8899";
    std::string stream_endpoint = "tcp://*:8900";
    std::string backend = "mock";
    std::string model_path;
    int max_new_tokens = 512;
    int max_context_len = 4096;
    bool show_help = false;
};

struct RouterRequest {
    std::string identity;
    std::string payload;
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
        "  --stream-endpoint <endpoint>   Streaming ROUTER endpoint\n"
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

        if (argument == "--stream-endpoint") {
            if (index + 1 >= argc) {
                throw std::runtime_error(
                    "missing value after --stream-endpoint"
                );
            }

            options.stream_endpoint = argv[++index];
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

    if (options.endpoint == options.stream_endpoint) {
        throw std::runtime_error(
            "normal and stream endpoints must be different"
        );
    }

    return options;
}

std::string messageToString(const zmq::message_t& message) {
    return std::string(
        static_cast<const char*>(message.data()), message.size()
    );
}

RouterRequest receiveRouterRequest(zmq::socket_t& socket) {
    zmq::message_t identity_message;
    zmq::message_t payload_message;

    const auto identity_received = socket.recv(
        identity_message,
        zmq::recv_flags::none
    );

    if (!identity_received.has_value()) {
        throw std::runtime_error(
            "failed to receive ROUTER identity"
        );
    }

    if (!socket.get(zmq::sockopt::rcvmore)) {
        throw std::runtime_error(
            "ROUTER request has no payload frame"
        );
    }

    const auto payload_received = socket.recv(
        payload_message,
        zmq::recv_flags::none
    );

    if (!payload_received.has_value()) {
        throw std::runtime_error(
            "failed to receive ROUTER payload"
        );
    }

    if (socket.get(zmq::sockopt::rcvmore)) {
        throw std::runtime_error(
            "ROUTER request has unexpected frames"
        );
    }

    return {
        messageToString(identity_message),
        messageToString(payload_message)
    };
}

void sendRouterMessage(
    zmq::socket_t& socket,
    const std::string& identity,
    const std::string& payload
) {
    const auto identity_sent = socket.send(
        zmq::buffer(identity),
        zmq::send_flags::sndmore
    );

    if (!identity_sent.has_value()) {
        throw std::runtime_error("failed to send ROUTER identity");
    }

    const auto payload_sent = socket.send(
        zmq::buffer(payload),
        zmq::send_flags::none
    );

    if (!payload_sent.has_value()) {
        throw std::runtime_error("failed to send ROUTER payload");
    }
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

        // RKLLM模型句柄通常不应该在没有明确线程安全保证时并发调用，所以两个端点共享一个事件循环
        // 正在非流式生成时，流式请求等待
        // 正在流式生成时，非流式请求等待
        // 每次只运行一个模型
        LlmService service(*backend);
        LlmStreamService stream_service(*backend);

        zmq::context_t context(1);

        zmq::socket_t normal_socket(
            context,
            zmq::socket_type::rep
        );

        zmq::socket_t stream_socket(
            context,
            zmq::socket_type::router
        );

        // linger控制socket关闭时，如何处理尚未发送完成，仍留在发送队列里的消息
        // 为0表示socket关闭时立即返回，丢弃所有还没有成功发送出去的排队消息，大于零表示等待的ms
        normal_socket.set(zmq::sockopt::linger, 0);
        stream_socket.set(zmq::sockopt::linger, 0);

        normal_socket.bind(options.endpoint);
        stream_socket.bind(options.stream_endpoint);

        std::cout << "[INFO] C++ LLM server started\n";
        std::cout
            << "[INFO] Normal endpoint: "
            << options.endpoint
            << '\n';
        std::cout
            << "[INFO] Stream endpoint: "
            << options.stream_endpoint
            << '\n';
        std::cout
            << "[INFO] Backend: "
            << backend->name()
            << '\n';

        // pollitem(socket, fd, events, revents)：
        // socket：ZMQ handle；events：关注可读事件；revents：当前还没有发生事件
        std::array<zmq::pollitem_t, 2> poll_items{{
            {
                normal_socket.handle(),
                0,
                ZMQ_POLLIN,
                0
            },
            {
                stream_socket.handle(),
                0,
                ZMQ_POLLIN,
                0
            }
        }};

        while (g_running.load()) {
            try {
                // 表示当前线程最多等待500毫秒，任意socket有消息，立即返回并把实际发生的事件写入每个元素的revents
                zmq::poll(
                    poll_items,
                    std::chrono::milliseconds(500)
                );

                if (poll_items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t request_message;

                    const auto received = normal_socket.recv(
                        request_message,
                        zmq::recv_flags::none
                    );

                    if (received.has_value()) {
                        const std::string request = messageToString(request_message);

                        const std::string response = service.handleMessage(request);

                        const auto sent = normal_socket.send(
                            zmq::buffer(response),
                            zmq::send_flags::none
                        );

                        if (!sent.has_value()) {
                            std::cerr
                                << "[WARNING] Failed to "
                                << "send normal response\n";
                        }
                    }
                }

                if (poll_items[1].revents & ZMQ_POLLIN) {
                    const RouterRequest request = receiveRouterRequest(stream_socket);

                    try {
                        stream_service.handleMessage(
                        request.payload,
                        [&stream_socket, &request](const std::string& event) {
                            sendRouterMessage(stream_socket, request.identity, event);
                        }
                        );
                    } catch (const std::exception& error) {
                        std::cerr
                            << "[WARNING] Invalid stream "
                            << "request: "
                            << error.what()
                            << '\n';
                    }
                }
            } catch (const zmq::error_t& error) {
                if (error.num() == EAGAIN || error.num() == EINTR) {
                    continue;
                }

                throw;  // 没有创建新异常，而是把当前捕获到的异常原样重新抛出
            }
        }

        normal_socket.close();
        stream_socket.close();
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
