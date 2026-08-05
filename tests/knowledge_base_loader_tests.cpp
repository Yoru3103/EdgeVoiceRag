#include "knowledge_base_loader.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testAliases(
    const std::string& knowledge_path
) {
    const KnowledgeBaseLoadResult result =
        loadDocumentChunks(knowledge_path);

    expect(
        result.ok,
        "failed to load knowledge base: "
            + result.error
    );

    expect(
        !result.documents.empty(),
        "knowledge base is empty"
    );

    const DocumentChunk& air_conditioner =
        result.documents.front();

    expect(
        air_conditioner.chunk_id == 0,
        "unexpected first chunk"
    );

    expect(
        !air_conditioner.aliases.empty(),
        "air conditioner aliases are empty"
    );

    expect(
        air_conditioner.retrieval_text.find(
            "车内太热"
        ) != std::string::npos,
        "retrieval_text does not contain alias"
    );

    expect(
        air_conditioner.text.find(
            "车内太热"
        ) == std::string::npos,
        "answer text must not contain aliases"
    );

    expect(
        air_conditioner.retrieval_text.find(
            air_conditioner.text
        ) == 0,
        "retrieval_text must begin with answer text"
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <chunks.json>\n";

        return 1;
    }

    try {
        testAliases(argv[1]);

        std::cout
            << "knowledge_base_loader_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "knowledge_base_loader_tests failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
