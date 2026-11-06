#include "knowledge_base_loader.h"

#include <fstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

using Json = nlohmann::json;

namespace {

KnowledgeBaseLoadResult failureResult(const std::string& error) {
    KnowledgeBaseLoadResult result;
    result.ok = false;
    result.error = error;

    return result;
}

std::string buildRetrievalText(
    const std::string& text,
    const std::vector<std::string>& aliases
) {
    if (aliases.empty()) {
        return text;
    }

    std::string retrieval_text = text + "\n相关表达: ";

    for (std::size_t index = 0; index < aliases.size(); index++) {
        if (index > 0) {
            retrieval_text += "；";
        }

        retrieval_text += aliases[index];
    }

    return retrieval_text;
}

} // namespace

KnowledgeBaseLoadResult loadDocumentChunks(const std::string& path) {
    std::ifstream input(path);

    if (!input.is_open()) {
        return failureResult(
            "failed to open knowledge base: " +
            path
        );
    }

    try {
        Json root;
        input >> root;

        if (!root.is_array()) {
            return failureResult(
                "knowledge base must contain "
                "a JSON array"
            );
        }

        std::vector<DocumentChunk> documents;
        std::unordered_set<int> loaded_ids;

        documents.reserve(root.size());

        for (const Json& item : root) {
            if (!item.is_object()) {
                return failureResult(
                    "knowledge base chunk "
                    "must be an object"
                );
            }

            if (
                !item.contains("chunk_id") ||
                !item.contains("title") ||
                !item.contains("content")
            ) {
                return failureResult(
                    "knowledge base chunk is "
                    "missing required fields"
                );
            }

            DocumentChunk chunk;

            chunk.chunk_id =
                item.at("chunk_id").get<int>();

            chunk.title =
                item.at("title").get<std::string>();

            chunk.content =
                item.at("content").get<std::string>();

            chunk.text = item.value(
                "text",
                chunk.title + ": " + chunk.content
            );

            if (item.contains("aliases")) {
                const Json& aliases_value = item.at("aliases");

                if (!aliases_value.is_array()) {
                    return failureResult(
                        "chunk aliases must be an array"
                    );
                }

                std::unordered_set<std::string> loaded_aliases;

                for (const Json& alias_value : aliases_value) {
                    if (!alias_value.is_string()) {
                        return failureResult(
                            "chunk alias must be a string"
                        );
                    }
                    const std::string alias = alias_value.get<std::string>();

                    if (alias.empty()) {
                        return failureResult(
                            "chunk alias must not be empty"
                        );
                    }

                    const bool inserted = loaded_aliases.insert(alias).second;

                    if (!inserted) {
                        return failureResult(
                            "duplicate alias in chunk: "
                            + std::to_string(chunk.chunk_id)
                        );
                    }

                    chunk.aliases.push_back(alias);
                }
            }

            const std::string expected_retrieval_text =
                buildRetrievalText(
                    chunk.text,
                    chunk.aliases
                );

            if (item.contains("retrieval_text")) {
                if (!item.at("retrieval_text").is_string()) {
                    return failureResult(
                        "chunk retrieval_text must be a string"
                    );
                }

                const std::string stored_retrieval_text =
                    item.at("retrieval_text").get<std::string>();

                if (
                    stored_retrieval_text != expected_retrieval_text
                ) {
                    return failureResult(
                        "chunk retrieval_text does not "
                        "match text and aliases: "
                        + std::to_string(chunk.chunk_id)
                    );
                }
            }

            chunk.retrieval_text = expected_retrieval_text;

            if (chunk.chunk_id < 0) {
                return failureResult(
                    "chunk_id must not be negative"
                );
            }

            if (
                chunk.title.empty() ||
                chunk.content.empty() ||
                chunk.text.empty() ||
                chunk.retrieval_text.empty()
            ) {
                return failureResult(
                    "knowledge base chunk contains "
                    "an empty text field"
                );
            }

            const bool inserted =
                loaded_ids.insert(
                    chunk.chunk_id
                ).second;

            if (!inserted) {
                return failureResult(
                    "duplicate chunk_id: " +
                    std::to_string(
                        chunk.chunk_id
                    )
                );
            }

            documents.push_back(
                std::move(chunk)
            );
        }

        if (documents.empty()) {
            return failureResult(
                "knowledge base contains no chunks"
            );
        }

        KnowledgeBaseLoadResult result;
        result.ok = true;
        result.documents =
            std::move(documents);

        return result;
    } catch (const Json::exception& error) {
        return failureResult(
            "invalid knowledge base JSON: " +
            std::string(error.what())
        );
    }
}
