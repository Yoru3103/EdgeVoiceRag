#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

#include "app_config.h"
#include "command_line_options.h"
#include "query_router.h"
#include "logger.h"
#include "perf_timer.h"
#include "rag_engine.h"
#include "rag_client_zmq.h"

static void logElapsedTime (const PerfTimer& timer) {
    std::ostringstream perf;
    perf << timer.name() << " handled in"
         << std::fixed << std::setprecision(3)
         << timer.elapsedMilliseconds() << " ms";

    Logger::log(LogLevel::Info, perf.str());
}

static std::string buildLocalRagAnswer(const std::vector<SearchResult>& results) {
    std::ostringstream answer;
    answer << "根据车辆手册：";

    if (results.empty()) {
        answer << "知识库中没有找到相关车辆手册内容。";
    } else {
        answer << "\n";

        for (size_t i = 0; i < results.size(); i++) {
            answer << i + 1 << ". " << results[i].document << " [score =" << results[i].score << "]";

            if (i + 1 < results.size()) {
                answer << "\n";
            }
        }
    }

    return answer.str();
}

static void handleQuery(const std::string& question,
                        const QueryRouter& router,
                        const RagEngine& rag_engine,
                        int top_k,
                        const std::string& rag_backend,
                        const std::string& rag_endpoint) {
    PerfTimer total_timer("single_query");

    QueryType type = router.classify(question);

    Logger::log(LogLevel::User, question);
    Logger::log(LogLevel::Route, router.typeToString(type));

    if (type == QueryType::VehicleManual) {
        if (rag_backend == "zmq") {
            PerfTimer rag_timer("rag_zmq_request");

            RagClilentZmq rag_client(rag_endpoint);
            std::string reply = rag_client.query(question);

            Logger::log(LogLevel::System, reply);
            logElapsedTime(rag_timer);
        } else {
            PerfTimer rag_timer("rag_local_request");

            std::vector<SearchResult> results = rag_engine.searchTopK(question, top_k);
            std::string answer = buildLocalRagAnswer(results);

            Logger::log(LogLevel::System, answer);
            logElapsedTime(rag_timer);
        }
    } else if (type == QueryType::chat) {
        Logger::log(LogLevel::System, "This is a chat question. LLM will handle it later.");
    } else {
        Logger::log(LogLevel::System, "Unknown query type.");
    }

    logElapsedTime(total_timer);
}

int main(int argc, char* argv[]) {
    CommandLineOptions options = CommandLineOptions::parse(argc, argv);

    std::string program_name = argc > 0 ? argv[0] : "edge_voice_rag";

    if (options.showHelp()) {
        std::cout << CommandLineOptions::usage(program_name);
        return 0;
    }

    if (options.hasError()) {
        Logger::log(LogLevel::Error, options.errorMessage());
        std::cout << CommandLineOptions::usage(program_name);
        return 1;
    }

    AppConfig config(options.configPath());

    if (!config.load()) {
        Logger::log(LogLevel::Error, "Failed to load config: " + options.configPath());
        return 1;
    }

    QueryRouter router;
    RagEngine rag_engine(config.knowledgePath());

    Logger::log(LogLevel::Info, "EdgeVoiceRAG started.");
    Logger::log(LogLevel::Info, "Config path: " + options.configPath());
    Logger::log(LogLevel::Info, "Knowledge path: " + config.knowledgePath());
    Logger::log(LogLevel::Info, "RAG backend: " + config.ragBackend());
    Logger::log(LogLevel::Info, "RAG endpoint: " + config.ragEndpoint());

    // 限制oss的局部作用域
    {
        std::ostringstream oss;
        oss << "RAG top_k: " << config.topK();
        Logger::log(LogLevel::Info, oss.str());
    }

    if (config.ragBackend() == "local") {
        if (!rag_engine.loadKnowledgeBase()) {
            Logger::log(LogLevel::Error, "Failed to load knowledge base: " + config.knowledgePath());
            return 1;
        }

        Logger::log(LogLevel::Info, "Knowledge base loaded successfully.");
    } else {
        Logger::log(LogLevel::Info, "Local knowledge base loading skipped because RAG backend is zmq.");
    }

    if (options.onceMode()) {
        handleQuery(
            options.onceQuery(), 
            router, 
            rag_engine, 
            config.topK(), 
            config.ragBackend(), 
            config.ragEndpoint()
        );
        return 0;
    }

    while (true) {
        std::cout << "\nPlease input your question, or type exit to quit:\n> ";

        std::string question;
        std::getline(std::cin, question);

        if (question == "exit") {
            std::cout << "Bye." << std::endl;
            break;
        }

        if (question.empty()) {
            Logger::log(LogLevel::Warning, "Empty query ignored.");
            continue;
        }
        
        handleQuery(
            question, 
            router, 
            rag_engine, 
            config.topK(), 
            config.ragBackend(),
            config.ragEndpoint()
    );
    }

    return 0;
}