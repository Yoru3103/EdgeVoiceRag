#include <iostream>
#include <string>

#include <zmq.hpp>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::req);

    const std::string endpoint = "tcp://localhost:5555";
    socket.connect(endpoint);

    std::cout << "ZeroMQ REQ client connected to " << endpoint << std::endl;

    while (true) {
        std::cout << "\nInput message, or type exit to stop server:\n> ";

        std::string question;
        std::getline(std::cin, question);

        if (question.empty()) {
            std::cout << "Empty message ignored." << std::endl;
            continue;
        }

        socket.send(zmq::buffer(question), zmq::send_flags::none);

        zmq::message_t reply_msg;
        auto recv_result = socket.recv(reply_msg, zmq::recv_flags::none);

        if (!recv_result.has_value()) {
            std::cerr << "Failed to receive reply." << std::endl;
            continue;
        }

        std::string reply(static_cast<const char*>(reply_msg.data()), reply_msg.size());

        std::cout << "[REPLY] " << reply << std::endl;

        if (question == "exit") {
            break;
        }
    }

    return 0;
}