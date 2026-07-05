#include <iostream>
#include <string>

#include "query_router.h"
#include "logger.h"

int main() {
    QueryRouter router;

    Logger::log(LogLevel::Info, "EdgeVoiceRAG started.");

    while (true) {
        std::cout << "\nPlease input your question, or type exit to quit:\n> ";

        std::string question;
        std::getline(std::cin, question);

        if (question == "exit") {
            std::cout << "Bye." << std::endl;
            break;
        }

        QueryType type = router.classify(question);

        Logger::log(LogLevel::User, question);
        Logger::log(LogLevel::Route, router.typeToString((type)));


        if (type == QueryType::VehicleManual) {
            Logger::log(LogLevel::System, "This is a vehicle manual question. RAG will handle it later.");
        } else if (type == QueryType::chat) {
            Logger::log(LogLevel::System, "This is a chat question. LLM will handle it later.");
        } else {
            Logger::log(LogLevel::System, "Unknown query type.");
        }
    }

    return 0;
}