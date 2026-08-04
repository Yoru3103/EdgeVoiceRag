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

            if (chunk.chunk_id < 0) {
                return failureResult(
                    "chunk_id must not be negative"
                );
            }

            if (
                chunk.title.empty() ||
                chunk.content.empty() ||
                chunk.text.empty()
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
