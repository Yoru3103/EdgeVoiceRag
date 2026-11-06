#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "app_config.h"
#include "bm25_retriever.h"
#include "command_line_options.h"
#include "edge_response_backend.h"
#include "logger.h"
#include "multi_level_response_system.h"
#include "perf_timer.h"
#include "zmq_text_client.h"

namespace {

void logElapsedTime(const PerfTimer& timer) {
    std::ostringstream message;

    message
        << timer.name()
        << " handled in "
        << std::fixed
        << std::setprecision(3)
        << timer.elapsedMilliseconds()
        << " ms";

    Logger::log(LogLevel::Perf, message.str());
}

void logProcessResult(
    const QueryProcessResult& result,
    const MultiLevelResponseSystem& system
) {
    Logger::log(LogLevel::Route, system.modeToString(result.mode));

    if (result.from_cache) {
        Logger::log(LogLevel::Info, "Response cache hit.");
    }

    if (result.ok) {
        Logger::log(LogLevel::System, result.answer);
        return;
    }

    Logger::log(LogLevel::Error, result.error);
}

void handleQuery(
    const std::string& query,
    MultiLevelResponseSystem& system
) {
    PerfTimer timer("multi_level_query");

    Logger::log(LogLevel::User, query);

    const QueryProcessResult result = (
        system.process(query)
    );

    logProcessResult(result, system);
    logElapsedTime(timer);
}

} // namespace

int main(int argc, char* argv[]) {
    const CommandLineOptions options = CommandLineOptions::parse(argc, argv);

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

    Bm25Retriever bm25_retriever(config.knowledgePath());

    if (config.ragBackend() == "local" && !bm25_retriever.loadKnowledgeBase()) {
        Logger::log(LogLevel::Error, "Failed to load knowledge base: " + config.knowledgePath());

        return 1;
    }

    ZmqTextClient requester;

    EdgeResponseBackend backend(
        bm25_retriever,
        requester,
        config.ragBackend(),
        config.ragEndpoint(),
        config.ragTimeoutMs(),
        config.llmEndpoint(),
        config.llmTimeoutMs(),
        config.topK()
    );

    MultiLevelResponseSystem system(backend);

    Logger::log(
        LogLevel::Info,
        "EdgeVoiceRAG multi-level "
        "response system started."
    );
    Logger::log(LogLevel::Info, "Config path: " + options.configPath());
    Logger::log(LogLevel::Info, "Knowledge path: " + config.knowledgePath());
    Logger::log(LogLevel::Info, "RAG backend: " + config.ragBackend());
    Logger::log(LogLevel::Info, "RAG endpoint: " + config.ragEndpoint());
    Logger::log(LogLevel::Info, "LLM endpoint: " + config.llmEndpoint());

    if (options.onceMode()) {
        handleQuery(
            options.onceQuery(),
            system
        );

        return 0;
    }

    while (true) {
        std::cout << "\nPlease input your query, or type exit to quit:\n> ";

        std::string query;
        std::getline(std::cin, query);

        if (query == "exit" || query == "quit" || query == "退出") {
            Logger::log(LogLevel::System, "Bye.");

            break;
        }

        if (query.empty()) {
            Logger::log(LogLevel::Warning, "Empty query ignored.");
            continue;
        }

        handleQuery(query, system);
    }

    return 0;
}