#include "rag_client_zmq.h"

#include <string>

#include <zmq.hpp>

RagClilentZmq::RagClilentZmq(const std::string& endpoint, int timeout)
    : endpoint_(endpoint)
    , timeout_ms_(timeout) {
}

std::string RagClilentZmq::query(const std::string& question) {
    try {
        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::req);

        socket.set(zmq::sockopt::sndtimeo, timeout_ms_);
        socket.set(zmq::sockopt::rcvtimeo, timeout_ms_);
        socket.set(zmq::sockopt::linger, 0);    // 默认情况下，ZeroMQ socket 关闭时可能会等待未发送消息被处理。设置为 0 表示关闭 socket 时不等待。
        
        socket.connect(endpoint_);

        auto send_result = socket.send(zmq::buffer(question), zmq::send_flags::none);

        if (!send_result.has_value()) {
            std::ostringstream oss;
            oss << "[RAG_ZMQ_ERROR] Failed to send request to " << endpoint_;
            return oss.str();
        }

        zmq::message_t reply_msg;
        auto recv_result = socket.recv(reply_msg, zmq::recv_flags::none);

        if (!recv_result.has_value()) {
            std::ostringstream oss;
            oss << "[RAG_ZMQ_ERROR] Timeout or no response from " << endpoint_
                << " within " << timeout_ms_ << " ms";
            return oss.str();
        }

        return std::string(
            static_cast<const char*>(reply_msg.data()),
            reply_msg.size()
        );
    } catch (const zmq::error_t& e) {
        std::ostringstream oss;
        oss << "[RAG_ZMQ_ERROR] ZeroMQ error: " << e.what();
        return oss.str();
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "[RAG_ZMQ_ERROR] Exception: " << e.what();
        return oss.str();
    }
}