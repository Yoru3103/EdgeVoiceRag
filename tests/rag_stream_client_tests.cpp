#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <zmq.hpp>

#include "rag_stream_client.h"
#include "rag_stream_protocol.h"

namespace {

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

std::string makeEndpoint(
    const std::string& test_name
) {
    return (
        "ipc:///tmp/edge_voice_rag_" +
        test_name +
        "_" +
        std::to_string(getpid()) +
        ".ipc"
    );
}

std::string messageToString(
    const zmq::message_t& message
) {
    return std::string(
        static_cast<const char*>(message.data()),
        message.size()
    );
}

void sendRouterMessage(
    zmq::socket_t& socket,
    const zmq::message_t& identity,
    const std::string& payload
) {
    socket.send(
        zmq::buffer(
            identity.data(),
            identity.size()
        ),
        zmq::send_flags::sndmore
    );

    socket.send(
        zmq::buffer(payload),
        zmq::send_flags::none
    );
}

void runSuccessfulMockServer(
    const std::string& endpoint,
    std::promise<void> ready
) {
    zmq::context_t context(1);

    zmq::socket_t socket(
        context,
        zmq::socket_type::router
    );

    socket.set(zmq::sockopt::linger, 0);
    socket.bind(endpoint);

    ready.set_value();

    zmq::message_t identity;
    zmq::message_t payload;

    socket.recv(
        identity,
        zmq::recv_flags::none
    );

    socket.recv(
        payload,
        zmq::recv_flags::none
    );

    const RagStreamRequest request =
        RagStreamProtocol::decodeRequest(
            messageToString(payload)
        );

    RagStreamEvent first_chunk;
    first_chunk.type = RagStreamEventType::Chunk;
    first_chunk.ok = true;
    first_chunk.request_id = request.request_id;
    first_chunk.sequence = 0;
    first_chunk.delta = "请打开";
    first_chunk.backend = "python_tfidf";
    first_chunk.llm_backend = "cpp_mock";
    first_chunk.elapsed_ms = 2.0;
    first_chunk.finished = false;

    RagStreamEvent second_chunk;
    second_chunk.type = RagStreamEventType::Chunk;
    second_chunk.ok = true;
    second_chunk.request_id = request.request_id;
    second_chunk.sequence = 1;
    second_chunk.delta = "空调控制界面。";
    second_chunk.backend = "python_tfidf";
    second_chunk.llm_backend = "cpp_mock";
    second_chunk.elapsed_ms = 4.0;
    second_chunk.finished = false;

    RagStreamEvent finished;
    finished.type = RagStreamEventType::Finished;
    finished.ok = true;
    finished.request_id = request.request_id;
    finished.sequence = 2;
    finished.answer = "请打开空调控制界面。";
    finished.backend = "python_tfidf";
    finished.llm_backend = "cpp_mock";
    finished.elapsed_ms = 6.0;
    finished.finished = true;

    sendRouterMessage(
        socket,
        identity,
        RagStreamProtocol::encodeEvent(first_chunk)
    );

    sendRouterMessage(
        socket,
        identity,
        RagStreamProtocol::encodeEvent(second_chunk)
    );

    sendRouterMessage(
        socket,
        identity,
        RagStreamProtocol::encodeEvent(finished)
    );
}

void runSequenceMismatchServer(
    const std::string& endpoint,
    std::promise<void> ready
) {
    zmq::context_t context(1);

    zmq::socket_t socket(
        context,
        zmq::socket_type::router
    );

    socket.set(zmq::sockopt::linger, 0);
    socket.bind(endpoint);

    ready.set_value();

    zmq::message_t identity;
    zmq::message_t payload;

    socket.recv(identity);
    socket.recv(payload);

    const RagStreamRequest request =
        RagStreamProtocol::decodeRequest(
            messageToString(payload)
        );

    RagStreamEvent event;
    event.type = RagStreamEventType::Chunk;
    event.ok = true;
    event.request_id = request.request_id;

    // 客户端期待的第一条序号应为 0。
    event.sequence = 1;

    event.delta = "错误序号";
    event.backend = "python_tfidf";
    event.llm_backend = "cpp_mock";
    event.elapsed_ms = 1.0;
    event.finished = false;

    sendRouterMessage(
        socket,
        identity,
        RagStreamProtocol::encodeEvent(event)
    );
}

void runErrorMockServer(
    const std::string& endpoint,
    std::promise<void> ready
) {
    zmq::context_t context(1);

    zmq::socket_t socket(
        context,
        zmq::socket_type::router
    );

    socket.set(zmq::sockopt::linger, 0);
    socket.bind(endpoint);

    ready.set_value();

    zmq::message_t identity;
    zmq::message_t payload;

    socket.recv(identity);
    socket.recv(payload);

    const RagStreamRequest request =
        RagStreamProtocol::decodeRequest(
            messageToString(payload)
        );

    RagStreamEvent event;
    event.type = RagStreamEventType::Error;
    event.ok = false;
    event.request_id = request.request_id;
    event.sequence = 0;
    event.backend = "python_tfidf";
    event.llm_backend = "cpp_mock";
    event.error = "generation cancelled";
    event.elapsed_ms = 3.0;
    event.finished = true;

    sendRouterMessage(
        socket,
        identity,
        RagStreamProtocol::encodeEvent(event)
    );
}

void testSuccessfulQuery() {
    const std::string endpoint =
        makeEndpoint("stream_success");

    std::promise<void> ready;
    std::future<void> ready_future =
        ready.get_future();

    std::thread server(
        runSuccessfulMockServer,
        endpoint,
        std::move(ready)
    );

    ready_future.wait();

    RagStreamClient client(
        endpoint,
        1000
    );

    std::vector<std::string> chunks;

    const RagStreamQueryResult result =
        client.query(
            RagStreamRequest{
                "rag-success",
                "空调怎么打开"
            },
            [&chunks](const RagStreamEvent& event) {
                if (
                    event.type ==
                    RagStreamEventType::Chunk
                ) {
                    chunks.push_back(event.delta);
                }
            }
        );

    server.join();

    expectTrue(
        result.ok,
        "successful stream query"
    );

    expectTrue(
        result.answer ==
            "请打开空调控制界面。",
        "assemble final answer"
    );

    expectTrue(
        chunks.size() == 2,
        "receive two stream chunks"
    );

    expectTrue(
        result.backend == "python_tfidf",
        "preserve RAG backend"
    );

    expectTrue(
        result.llm_backend == "cpp_mock",
        "preserve LLM backend"
    );
}

void testRejectSequenceMismatch() {
    const std::string endpoint =
        makeEndpoint("sequence_mismatch");

    std::promise<void> ready;
    std::future<void> ready_future =
        ready.get_future();

    std::thread server(
        runSequenceMismatchServer,
        endpoint,
        std::move(ready)
    );

    ready_future.wait();

    RagStreamClient client(
        endpoint,
        1000
    );

    const RagStreamQueryResult result =
        client.query(
            RagStreamRequest{
                "rag-sequence",
                "测试错误序号"
            }
        );

    server.join();

    expectTrue(
        !result.ok,
        "sequence mismatch returns failure"
    );

    expectTrue(
        result.error.find("sequence mismatch") !=
            std::string::npos,
        "sequence mismatch contains reason"
    );
}

void testServerError() {
    const std::string endpoint =
        makeEndpoint("stream_error");

    std::promise<void> ready;
    std::future<void> ready_future =
        ready.get_future();

    std::thread server(
        runErrorMockServer,
        endpoint,
        std::move(ready)
    );

    ready_future.wait();

    RagStreamClient client(
        endpoint,
        1000
    );

    const RagStreamQueryResult result =
        client.query(
            RagStreamRequest{
                "rag-error",
                "测试取消"
            }
        );

    server.join();

    expectTrue(
        !result.ok,
        "server error returns failure"
    );

    expectTrue(
        result.error == "generation cancelled",
        "server error message is preserved"
    );
}

void testRejectInvalidConfiguration() {
    bool endpoint_rejected = false;

    try {
        RagStreamClient client("", 1000);
    } catch (const std::invalid_argument&) {
        endpoint_rejected = true;
    }

    expectTrue(
        endpoint_rejected,
        "reject empty endpoint"
    );

    bool timeout_rejected = false;

    try {
        RagStreamClient client(
            "tcp://localhost:5557",
            0
        );
    } catch (const std::invalid_argument&) {
        timeout_rejected = true;
    }

    expectTrue(
        timeout_rejected,
        "reject invalid timeout"
    );
}

}  // namespace

int main() {
    testSuccessfulQuery();
    testRejectSequenceMismatch();
    testServerError();
    testRejectInvalidConfiguration();

    if (failed_count == 0) {
        std::cout
            << "\nAll RAG stream client tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}