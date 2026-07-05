#include <iostream>
#include <string>

int main() {
    std::cout << "EdgeVoiceRAG started." << std::endl;

    while (true) {
        std::cout << "\nPlease input your question, or type exit to quit:\n> ";

        std::string question;
        std::getline(std::cin, question);

        if (question == "exit") {
            std::cout << "Bye." << std::endl;
            break;
        }

        std::cout << "[USER]" << question << std::endl;
        std::cout << "[SYSTEM] This is a mock answer." << std::endl;
    }

    return 0;
}