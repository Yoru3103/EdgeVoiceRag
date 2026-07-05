#include <iostream>
#include <string>

#include "query_router.h"

int main() {
    QueryRouter router;

    std::cout << "EdgeVoiceRAG started." << std::endl;

    while (true) {
        std::cout << "\nPlease input your question, or type exit to quit:\n> ";

        std::string question;
        std::getline(std::cin, question);

        if (question == "exit") {
            std::cout << "Bye." << std::endl;
            break;
        }

        QueryType type = router.classify(question);

        std::cout << "[USER] " << question << std::endl;
        std::cout << "[ROUTE] " << router.typeToString(type) << std::endl;

        if (type == QueryType::VehicleManual) {
            std::cout << "[SYSTEM] This is a vehicle manual question. RAG will handle it later." << std::endl;
        } else if (type == QueryType::chat) {
            std::cout << "[SYSTEM] This is a chat question. LLM will handle it later." << std::endl;
        } else {
            std::cout << "[SYSTEM] Unknown query type." << std::endl;
        }
    }

    return 0;
}