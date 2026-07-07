#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#include <zmq.hpp>

#include "rag_engine.h"
#include "app_config.h"
#include "logger.h"
#include "perf_timer.h"

static std::string messageToString(const zmq::message_t& message) {
    return std::string(
        static_cast<const char*>(message.data()),
        message.size()
    );
}

static std::string buildRagAnswer(const std::vector<SearchResult>& results) {
    std::ostringstream answer;

    answer << "根据车辆手册：";

    if (results.empty()) {
        answer << "知识库没有找到相关车辆手册内容。";
        return answer.str();
    }

    answer << "\n";

    for (size_t i = 0; i < results.size(); i++) {
        answer << i + 1 << ". " << results[i].document << " [score=" << results[i].score << "]";

        if (i + 1 < results.size()) {
            answer << "\n";
        }
    }

    return answer.str();
}

int main(int argc, char *argv[]) {
    std::string config_path = "config/app.conf";

    if (argc == 3 && std::string(argv[1]) == "--config") {
        config_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage:\n"
                  << " " << argv[0] << "\n"
                  << " " << argv[0] << " --config config/app.conf\n";
        return 1;
    }

    AppConfig config(config_path);

    if (!config.load()) {
        Logger::log(LogLevel::Error, "Failed to load config: " + config_path);
        return 1;
    }

    RagEngine rag_engine(config.knowledgePath());

    if (!rag_engine.loadKnowledgeBase()) {
        Logger::log(LogLevel::Error, "Failed to load knowledge base: " + config.knowledgePath());
        return 1;
    }

    Logger::log(LogLevel::Info, "RAG server started.");
    Logger::log(LogLevel::Info, "Config path: " + config_path);
    Logger::log(LogLevel::Info, "Knowledge path: " + config.knowledgePath());

    {
        std::ostringstream oss;
        oss << "RAG top_k: " << config.topK();
        Logger::log(LogLevel::Info, oss.str());
    }

    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);

    const std::string endpoint = "tcp://*:5555";
    socket.bind(endpoint);

    Logger::log(LogLevel::Info, "RAG server listening on " + endpoint);

    while (true) {
        zmq::message_t request_msg;
        auto recv_result = socket.recv(request_msg, zmq::recv_flags::none);

        if (!recv_result.has_value()) {
            Logger::log(LogLevel::Warning, "Failed to receive request.");
            continue;
        }

        std::string query = messageToString(request_msg);

        Logger::log(LogLevel::User, query);

        if (query == "exit") {
            std::string reply = "rag_server exiting";
            socket.send(zmq::buffer(reply), zmq::send_flags::none);
            break;
        }

        PerfTimer timer("rag_server_search");

        std::vector<SearchResult> results = rag_engine.searchTopK(query, config.topK());
        std::string reply = buildRagAnswer(results);

        socket.send(zmq::buffer(reply), zmq::send_flags::none);

        std::ostringstream perf;
        perf << timer.name() << " handled in "
             << timer.elapsedMilliseconds() << " ms";
        Logger::log(LogLevel::Perf, perf.str());
    }

    Logger::log(LogLevel::Info, "RAG server stopped.");

    return 0;
}