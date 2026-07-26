#include "rag_stream_client.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include <zmq.hpp>

#include "rag_stream_protocol.h"

namespace {

std::string messageToString(const zmq::message_t& message) {
    return std::string(static_cast<const char*>(message.data()), message.size());
}

void validateClientOptions(const std::string& endpoint, int timeout_ms) {
    if (endpoint.empty()) {
        throw std::invalid_argument("RAG stream endpoint must not be empty");
    }

    if (timeout_ms <= 0) {
        throw std::invalid_argument("RAG stream timeout must be greater than zero");
    }
}

void validateEventIdentity(const RagStreamEvent& event, const RagStreamRequest& request) {
    if (event.request_id != request.request_id) {
        throw std::runtime_error(
            "RAG stream response request_id mismatch: "
            "expected " +
            request.request_id +
            ", received " +
            event.request_id
        );
    }
}

void validateSequence(const RagStreamEvent& event, std::size_t expected_sequence) {
    if (event.sequence != expected_sequence) {
        throw std::runtime_error(
            "RAG stream sequence mismatch: expected " +
            std::to_string(expected_sequence) +
            ", received " +
            std::to_string(event.sequence)
        );
    }
}

}   // namespace

RagStreamClient::RagStreamClient(
    std::string endpoint,
    int timeout_ms
)
    : endpoint_(std::move(endpoint))
    , timeout_ms_(timeout_ms) {
    validateClientOptions(endpoint_, timeout_ms_);
}

std::string RagStreamClient::name() const {
    return "zmq_rag_stream";
}

// ZeroMQ一般不应该跨线程共享，未来语音生成、主线程检测用户打断以及曲线线程发送cancel的socket应该相互独立。
// 本模块每次调用应创建自己的socket
RagStreamQueryResult RagStreamClient::query(
    const RagStreamRequest& request,
    const RagStreamEventHandler& handler
) const {
    try {
        const std::string encoded_request = RagStreamProtocol::encodeRequest(request);

        zmq::context_t context(1);
        // 发送一条查询后需要接收多条事件，因此使用dealer
        zmq::socket_t socket(context, zmq::socket_type::dealer);

        socket.set(zmq::sockopt::sndtimeo, timeout_ms_);
        socket.set(zmq::sockopt::rcvtimeo, timeout_ms_);
        socket.set(zmq::sockopt::linger, 0);

        socket.connect(endpoint_);

        const auto sent = socket.send(
            zmq::buffer(encoded_request),
            zmq::send_flags::none
        );

        if (!sent.has_value()) {
            return RagStreamQueryResult::failure(
                request.request_id,
                "failed to send RAG stream request to " + endpoint_
            );
        }

        std::size_t expected_sequence = 0;
        std::string accumulated_answer;

        while (true) {
            zmq::message_t reply;

            const auto received = socket.recv(reply, zmq::recv_flags::none);

            if (!received.has_value()) {
                return RagStreamQueryResult::failure(
                    request.request_id,
                    "RAG stream request timed out after " +
                    std::to_string(timeout_ms_) +
                    " ms: " +
                    endpoint_
                );
            }

            const RagStreamEvent event = RagStreamProtocol::decodeEvent(messageToString(reply));
            validateEventIdentity(event, request);
            validateSequence(event, expected_sequence);

            if (handler) {
                handler(event);
            }

            switch (event.type) {
                case RagStreamEventType::Chunk:
                    accumulated_answer += event.delta;
                    expected_sequence++;
                    break;

                case RagStreamEventType::Finished:
                    if (accumulated_answer != event.answer) {
                        return RagStreamQueryResult::failure(
                            request.request_id,
                            "RAG stream chunks do not "
                            "match final answer"
                        );
                    }

                    return RagStreamQueryResult::success(
                        request.request_id,
                        event.answer,
                        event.backend,
                        event.llm_backend,
                        event.elapsed_ms
                    );

                case RagStreamEventType::Error:
                    return RagStreamQueryResult::failure(
                        request.request_id,
                        event.error
                    );
            }
        }
    } catch (const zmq::error_t& error) {
        return RagStreamQueryResult::failure(
            request.request_id,
            "RAG stream ZeroMQ error: " +
            std::string(error.what())
        );
    } catch (const std::exception& error) {
        return RagStreamQueryResult::failure(
            request.request_id,
            error.what()
        );
    }
}

const std::string& RagStreamClient::endpoint() const {
    return endpoint_;
}

int RagStreamClient::timeoutMilliseconds() const {
    return timeout_ms_;
}
