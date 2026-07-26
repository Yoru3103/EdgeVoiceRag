/*
    C++->Python: 把查询编码requery JSON
    Python->C++: 吧rag JSON解析为强类型C++对象    
*/
#pragma once

#include <cstddef>
#include <string>

enum class RagStreamEventType {
    Chunk,
    Finished,
    Error,
};

struct RagStreamRequest {
    std::string request_id;
    std::string query;

    RagStreamRequest() : request_id(""), query("") {}
    RagStreamRequest(const std::string& request_id, const std::string& query) : request_id(request_id), query(query) {}
};

struct RagStreamEvent {
    RagStreamEventType type = RagStreamEventType::Error;

    bool ok = false;

    std::string request_id;
    std::size_t sequence = 0;

    std::string delta;
    std::string answer;

    std::string backend;        // rag后端
    std::string llm_backend;    // 生成后端

    std::string error;

    double elapsed_ms = 0.0;
    bool finished = false;
};

class RagStreamProtocol {
public:
    static constexpr int KVersion = 1;

    static std::string encodeRequest(const RagStreamRequest& request);
    static RagStreamRequest decodeRequest(const std::string& message);

    static std::string encodeEvent(const RagStreamEvent& event);
    static RagStreamEvent decodeEvent(const std::string& message);
    static std::string eventTypeToString(RagStreamEventType type);
};
