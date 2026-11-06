#include "llm_control_server.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

namespace {

constexpr int kProtocolVersion = 1;

std::string messageToString(const zmq::message_t& message) {
    return std::string(static_cast<const char*>(message.data()), message.size());
}

}   // namespace

LlmControlServer::LlmControlServer(
    LlmStreamService& stream_service,
    std::string endpoint
)
    : stream_service_(stream_service)
    , endpoint_(std::move(endpoint)) {
    if (endpoint_.empty()) {
        throw std::invalid_argument(
            "LLM control endpoint "
            "must not be empty"
        );
    }
}

LlmControlServer::~LlmControlServer() {
    stop();
}

void LlmControlServer::start() {
    if (running_.exchange(true)) {
        throw std::runtime_error(
            "LLM control server "
            "is already running"
        );
    }
    
    // 主线程执行rkllm_run()，无法处理新的网络请求
    worker_ = std::thread(
        [this]() {
            run();
        }
    );
}

void LlmControlServer::stop() {
    running_.store(false);

    if (worker_.joinable()) {
        worker_.join();
    }
}

bool LlmControlServer::running() const {
    return running_.load();
}

void LlmControlServer::run() {
    try {
        zmq::context_t context(1);
        zmq::socket_t socket(
            context,
            zmq::socket_type::rep
        );

        socket.set(zmq::sockopt::linger, 0);
        socket.set(zmq::sockopt::rcvtimeo, 200);
        
        socket.bind(endpoint_);

        std::cout
            << "[INFO] LLM control endpoint: "
            << endpoint_
            << '\n';

        while (running_.load()) {
            zmq::message_t request_message;

            try {
                const auto received = socket.recv(
                    request_message,
                    zmq::recv_flags::none
                );

                if (!received.has_value()) {
                    continue;
                }

                const std::string response = handleMessage(messageToString(request_message));

                socket.send(
                    zmq::buffer(response),
                    zmq::send_flags::none
                );
            } catch (const zmq::error_t& error) {
                if (error.num() == EAGAIN || error.num() == EINTR) {
                    continue;
                }

                throw;
            }
        }

        socket.close();
        context.close();
    } catch (const std::exception& error) {
        running_.store(false);

        std::cerr
            << "[ERROR] LLM control server: "
            << error.what()
            << '\n';
    }
}

std::string LlmControlServer::handleMessage(const std::string& message) {
    std::string request_id;

    try {
        const auto request = nlohmann::json::parse(message);

        if (request.value("version", 0) != kProtocolVersion) {
            throw std::runtime_error(
                "unsupported cancel "
                "protocol version"
            );
        }

        if (request.value("type", "") != "cancel") {
            throw std::runtime_error(
                "unsupported control "
                "request type"
            );
        }

        request_id = request.value("request_id", "");
        if (request_id.empty()) {
            throw std::runtime_error("missing cancel request_id");
        }

        const bool cancelled = stream_service_.cancel(request_id);
        return nlohmann::json{
            {"version", kProtocolVersion},
            {"type", "cancel_result"},
            {"ok", true},
            {"request_id", request_id},
            {"cancelled", cancelled},
            {"active_request_id", stream_service_.activeRequestId()},
            {"error", ""}
        }.dump();
    } catch (const std::exception& error) {
        return nlohmann::json{
            {"version", kProtocolVersion},
            {"type", "cancel_result"},
            {"ok", false},
            {"request_id", request_id},
            {"cancelled", false},
            {"active_request_id", stream_service_.activeRequestId()},
            {"error", error.what()}
        }.dump();
    }
}
