#pragma once

#include <string>

#include "response_backend.h"

class MockResponseBackend : public ResponseBackend {
public:
    BackendResult rag_result = BackendResult::success("mock rag answer");

    BackendResult llm_result = BackendResult::success("mock llm answer");

    int rag_call_count = 0;
    int llm_call_count = 0;

    std::string last_rag_query;
    std::string last_llm_prompt;

    BackendResult searchRag(const std::string& query) override {
        rag_call_count++;
        last_rag_query = query;

        return rag_result;
    }

    BackendResult generateLlm(const std::string& prompt) override {
        llm_call_count++;
        last_llm_prompt = prompt;

        return llm_result;
    }
};