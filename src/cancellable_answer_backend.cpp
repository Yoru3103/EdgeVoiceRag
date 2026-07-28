#include "cancellable_answer_backend.h"

CancellationResult CancellationResult::success(
    const std::string& request_id,
    bool cancelled,
    const std::string& active_request_id
) {
    CancellationResult result;

    result.ok = true;
    result.cancelled = cancelled;
    result.request_id = request_id;
    result.active_request_id = active_request_id;

    return result;
}

CancellationResult CancellationResult::failure(
    const std::string& request_id,
    const std::string& error
) {
    CancellationResult result;
    
    result.ok = false;
    result.cancelled = false;
    result.request_id = request_id;
    result.error = error;

    return result;
}