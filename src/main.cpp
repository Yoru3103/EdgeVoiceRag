#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>

#include "app_config.h"
#include "command_line_options.h"
#include "query_router.h"
#include "logger.h"
#include "perf_timer.h"
#include "rag_engine.h"

static void logElapsedTime (const PerfTimer& timer) {
    std::ostringstream perf;
    perf << timer.name() << " handled in"
         << std::fixed << std::setprecision(3)
         << timer.elapsedMilliseconds() << " ms";

    Logger::log(LogLevel::Info, perf.str());
}

static void handleQuery(const std::string& question,
                        const QueryRouter& router,
                        const RagEngine& rag_engine,
                        int top_k) {
    PerfTimer total_timer("single_query");

    QueryType type = router.classify(question);

    Logger::log(LogLevel::User, question);
    Logger::log(LogLevel::Route, router.typeToString(type));

    if (type == QueryType::VehicleManual) {
        PerfTimer rag_timer("rag_search");

        std::vector<SearchResult> results = rag_engine.searchTopK(question, top_k);

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

        Logger::log(LogLevel::System, answer.str());
        logElapsedTime(rag_timer);
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

    // 限制oss的局部作用域
    {
        std::ostringstream oss;
        oss << "RAG top_k: " << config.topK();
        Logger::log(LogLevel::Info, oss.str());
    }

    if (!rag_engine.loadKnowledgeBase()) {
        Logger::log(LogLevel::Error, "Failed to load knowledge base: " + config.knowledgePath());
        return 1;
    }

    Logger::log(LogLevel::Info, "Knowledge base loaded successfully.");

    if (options.onceMode()) {
        handleQuery(options.onceQuery(), router, rag_engine, config.topK());
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
        
        handleQuery(question, router, rag_engine, config.topK());
    }

    return 0;
}