#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>

#include "query_router.h"
#include "logger.h"
#include "perf_timer.h"
#include "rag_engine.h"

int main() {
    QueryRouter router;
    RagEngine rag_engine("docs/vehicle_manual.txt");

    Logger::log(LogLevel::Info, "EdgeVoiceRAG started.");

    if (!rag_engine.loadKnowledgeBase()) {
        Logger::log(LogLevel::Error, "Failed to load knowledge base: docs/vehicle_manual.txt");
        return 1;
    }

    Logger::log(LogLevel::Info, "Knowledge base loaded successfully.");

    while (true) {
        std::cout << "\nPlease input your question, or type exit to quit:\n> ";

        std::string question;
        std::getline(std::cin, question);

        if (question == "exit") {
            std::cout << "Bye." << std::endl;
            break;
        }

        PerfTimer timer("single_query");

        QueryType type = router.classify(question);

        Logger::log(LogLevel::User, question);
        Logger::log(LogLevel::Route, router.typeToString((type)));


        if (type == QueryType::VehicleManual) {
            PerfTimer rag_timer("rag_search");

            std::vector<SearchResult> results = rag_engine.searchTopK(question, 3);

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

            std::ostringstream rag_perf;
            rag_perf << rag_timer.name() << "handle_in " << std::fixed << std::setprecision(3) << rag_timer.elapsedMilliseconds() << "ms";

            Logger::log(LogLevel::Perf, rag_perf.str());
        } else if (type == QueryType::chat) {
            Logger::log(LogLevel::System, "This is a chat question. LLM will handle it later.");
        } else {
            Logger::log(LogLevel::System, "Unknown query type.");
        }

        // 字符串流，用来拼接字符串
        std::ostringstream oss;
        // std::fixed << std::setprecision(3)控制timer时间只显示到小数点后三位
        oss << timer.name() << "handle in " << std::fixed << std::setprecision(3) << timer.elapsedMilliseconds() << "ms";

        Logger::log(LogLevel::Perf, oss.str());
    }

    return 0;
}