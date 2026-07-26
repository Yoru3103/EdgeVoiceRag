#include  "streaming_answer_backend.h"

RagStreamQueryResult RagStreamQueryResult::success(
    const std::string& request_id,
    const std::string& answer,
    const std::string& backend,
    const std::string& llm_backend,
    double elapsed_ms
) {
    RagStreamQueryResult result;

    result.ok = true;
    result.request_id = request_id;
    result.answer = answer;
    result.backend = backend;
    result.llm_backend = llm_backend;
    result.elapsed_ms = elapsed_ms;

    return result;
}

RagStreamQueryResult RagStreamQueryResult::failure(
    const std::string& request_id,
    const std::string& error
) {
    RagStreamQueryResult result;

    result.ok = false;
    result.request_id = request_id;
    result.error = error;

    return result;
}
