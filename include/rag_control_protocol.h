#pragma once

#include <string>

struct RagCancelRequest {
    std::string request_id;
};

struct RagCancelResponse {
    bool ok = false;
    bool cancelled = false;

    std::string request_id;
    std::string active_request_id;
    std::string error;
};

class RagControlProtocol {
public:
    // constexpr：要求值不能修改，且必须在编译期就能够确定该值
    static constexpr int KVersion = 1;

    static std::string encodeRequest(const RagCancelRequest& request);
    static RagCancelRequest decodeRequest(const std::string& message);

    static std::string encodeResponse(const RagCancelResponse& response);
    static RagCancelResponse decodeResponse(const std::string& message);
};
