#include <iostream>
#include <string>

#include <zmq.hpp>

static std::string messageToString(const zmq::message_t& message) {
    return std::string(
        static_cast<const char*>(message.data()),
        message.size()
    );
}

int main(int argc, char *argv[]) {
    std::string endpoint = "tcp://localhost:5555";

    if (argc == 3 && std::string(argv[1])  == "--endpoint") {
        endpoint = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << "\n"
                  << "  " << argv[0] << " --endpoint tcp://localhost:5555\n";
        return 1;
    }

    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::req);

    socket.connect(endpoint);

    std::cout << "RAG client connected to " << endpoint << std::endl;

    while (true) {
        std::cout << "\nInput query, or type exit to stop server:\n> ";

        std::string query;
        std::getline(std::cin, query);

        if (query.empty()) {
            std::cout << "Empty query ignored." << std::endl;
            continue;
        }

        socket.send(zmq::buffer(query), zmq::send_flags::none);

        zmq::message_t reply_msg;
        auto recv_result = socket.recv(reply_msg, zmq::recv_flags::none);

        if (!recv_result.has_value()) {
            std::cerr << "Failed to receive reply." << std::endl;
            continue;
        }

        std::string reply = messageToString(reply_msg);

        std::cout << "[RAG_REPLY]\n" << reply << std::endl;

        if (query == "exit") {
            break;
        }
    }

    return 0;
}