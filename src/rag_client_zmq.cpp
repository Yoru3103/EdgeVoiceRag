#include "rag_client_zmq.h"

#include <string>

#include <zmq.hpp>

RagClilentZmq::RagClilentZmq(const std::string& endpoint)
    :endpoint_(endpoint) {
}

std::string RagClilentZmq::query(const std::string& question) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::req);

    socket.connect(endpoint_);

    socket.send(zmq::buffer(question), zmq::send_flags::none);

    zmq::message_t reply_msg;

    auto recv_result = socket.recv(reply_msg, zmq::recv_flags::none);

    if (!recv_result.has_value()) {
        return "RAG server did not return a valid response.";
    }

    return std::string(
        static_cast<const char*>(reply_msg.data()),
        reply_msg.size()
    );
}