#include <iostream>
#include <string>

#include <zmq.hpp>

int main() {
    zmq::context_t context(1);  //ZeroMQ 运行时上下文。管理运行时的内部I/O线程和资源
    zmq::socket_t socket(context, zmq::socket_type::rep);   // 内部封装了连接管理、消息收发、队列、重连、传输协议

    const std::string endpoint = "tcp://*:5555";
    socket.bind(endpoint);

    std::cout << "ZeroMQ REO server listening on " << endpoint <<std::endl;

    while (true) {
        zmq::message_t request;
        
        auto recv_result = socket.recv(request, zmq::recv_flags::none);

        if (!recv_result.has_value()) {
            std::cerr << "Failed to receive message." << std::endl;
            continue;
        }

        std::string question(static_cast<const char*>(request.data()), request.size());

        std::cout << "[REQUEST] " << question << std::endl;

        if (question == "exit") {
            std::string reply = "server exiting";
            socket.send(zmq::buffer(reply), zmq::send_flags::none);
            break;
        }

        std::string reply = "server received: " + question;
        socket.send(zmq::buffer(reply), zmq::send_flags::none);
    }

    std::cout << "ZeroMQ REP server stopped." << std::endl;

    return 0;
}