#include "zmq_text_client.h"

#include <exception>
#include <string>

#include <zmq.hpp>

BackendResult ZmqTextClient::request(
    const std::string& endpoint,
    const std::string& text,
    int timeout_ms
) {
    try {
        zmq::context_t context(1);
        zmq::socket_t socket(
            context,
            zmq::socket_type::req
        );

        socket.set(
            zmq::sockopt::sndtimeo,
            timeout_ms
        );

        socket.set(
            zmq::sockopt::rcvtimeo,
            timeout_ms
        );

        socket.set(zmq::sockopt::linger, 0);
        socket.connect(endpoint);

        const auto sent = socket.send(
            zmq::buffer(text),
            zmq::send_flags::none
        );

        if (!sent.has_value()) {
            return BackendResult::failure(
                "failed to send request to "
                + endpoint
            );
        }

        zmq::message_t reply;

        const auto received = socket.recv(
            reply,
            zmq::recv_flags::none
        );

        if (!received.has_value()) {
            return BackendResult::failure(
                "request timed out after "
                + std::to_string(timeout_ms)
                + " ms: "
                + endpoint
            );
        }

        return BackendResult::success(
            std::string(
                static_cast<const char*>(reply.data()),
                reply.size()
            )
        );
    } catch (const zmq::error_t& error) {
        return BackendResult::failure(
            "ZeroMQ error: "
            + std::string(error.what())
        );
    } catch (const std::exception& error) {
        return BackendResult::failure(
            error.what()
        );
    }
}