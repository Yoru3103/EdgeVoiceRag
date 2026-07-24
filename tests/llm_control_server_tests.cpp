#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>
#include <unistd.h>
#include <zmq.hpp>

#include "llm_control_server.h"

namespace {

class CancellableBackend : public LlmBackend {
public:
    std::string name() const override {
        return "cancellable";
    }

    LlmGenerationResult generate(const std::string& prompt) override {
        (void)prompt;
        return LlmGenerationResult::failure("non-stream generation unused");
    }

    LlmGenerationResult generateStream(
        const std::string& prompt,
        const LlmChunkCallback& callback
    ) override {
        (void)prompt;
        running_.store(true);
        cancelled_.store(false);
        callback("测试");

        while (!cancelled_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        running_.store(false);
        return LlmGenerationResult::failure("generation cancelled");
    }

    bool cancel() override {
        if (!running_.load()) {
            return false;
        }

        cancelled_.store(true);
        return true;
    }

    bool running() const {
        return running_.load();
    }

private:
    std::atomic_bool running_{false};
    std::atomic_bool cancelled_{false};
};

int failed_count = 0;

void expectTrue(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    failed_count++;
}

std::string makeEndpoint() {
    constexpr int first_test_port = 20000;
    constexpr int test_port_count = 20000;
    const int port = first_test_port +
        static_cast<int>(getpid() % test_port_count);

    return "tcp://127.0.0.1:" + std::to_string(port);
}

nlohmann::json sendRequest(
    const std::string& endpoint,
    const nlohmann::json& request
) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::req);

    socket.set(zmq::sockopt::linger, 0);
    socket.set(zmq::sockopt::sndtimeo, 1000);
    socket.set(zmq::sockopt::rcvtimeo, 1000);
    socket.connect(endpoint);
    socket.send(zmq::buffer(request.dump()), zmq::send_flags::none);

    zmq::message_t response;
    const auto received = socket.recv(response, zmq::recv_flags::none);

    if (!received.has_value()) {
        throw std::runtime_error("control response timed out");
    }

    return nlohmann::json::parse(
        std::string(
            static_cast<const char*>(response.data()),
            response.size()
        )
    );
}

bool waitForBackend(const CancellableBackend& backend) {
    for (int index = 0; index < 1000; index++) {
        if (backend.running()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

void testInactiveRequestReturnsFalse(const std::string& endpoint) {
    const auto response = sendRequest(
        endpoint,
        {
            {"version", 1},
            {"type", "cancel"},
            {"request_id", "inactive-request"}
        }
    );

    expectTrue(response.value("ok", false), "valid request succeeds");
    expectTrue(
        !response.value("cancelled", true),
        "inactive request is not cancelled"
    );
    expectTrue(
        response.value("request_id", "") == "inactive-request",
        "response preserves request id"
    );
}

void testInvalidRequestReturnsError(const std::string& endpoint) {
    const auto response = sendRequest(
        endpoint,
        {
            {"version", 1},
            {"type", "unknown"},
            {"request_id", "invalid-request"}
        }
    );

    expectTrue(!response.value("ok", true), "invalid type is rejected");
    expectTrue(
        !response.value("error", "").empty(),
        "invalid request contains error"
    );
}

void testServerSurvivesIdleTimeout(const std::string& endpoint) {
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    testInactiveRequestReturnsFalse(endpoint);
}

void testActiveRequestIsCancelled(
    const std::string& endpoint,
    CancellableBackend& backend,
    LlmStreamService& service
) {
    const std::string request_id = "active-request";
    const nlohmann::json stream_request{
        {"version", 1},
        {"type", "generate"},
        {"request_id", request_id},
        {"prompt", "测试取消"},
        {"stream", true}
    };

    std::thread generation_thread(
        [&service, message = stream_request.dump()]() {
            service.handleMessage(message, [](const std::string&) {});
        }
    );

    const bool backend_started = waitForBackend(backend);
    expectTrue(backend_started, "stream generation becomes active");

    if (!backend_started) {
        backend.cancel();
        generation_thread.join();
        return;
    }

    const auto response = sendRequest(
        endpoint,
        {
            {"version", 1},
            {"type", "cancel"},
            {"request_id", request_id}
        }
    );

    expectTrue(response.value("ok", false), "cancel request succeeds");
    expectTrue(
        response.value("cancelled", false),
        "active request is cancelled"
    );

    generation_thread.join();

    expectTrue(
        service.activeRequestId().empty(),
        "active request is cleared"
    );
}

}   // namespace

int main() {
    const std::string endpoint = makeEndpoint();

    CancellableBackend backend;
    LlmStreamService service(backend);
    LlmControlServer server(service, endpoint);

    try {
        server.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        testServerSurvivesIdleTimeout(endpoint);
        testInvalidRequestReturnsError(endpoint);
        testActiveRequestIsCancelled(endpoint, backend, service);
    } catch (const std::exception& error) {
        std::cout << "[FAIL] unexpected exception: " << error.what() << '\n';
        failed_count++;
    }

    server.stop();

    if (failed_count == 0) {
        std::cout << "\nAll LLM control server tests passed.\n";
        return 0;
    }

    std::cout << "\nFailed tests: " << failed_count << '\n';
    return 1;
}
