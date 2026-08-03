#include <iostream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "edge_response_backend.h"
#include "bm25_retriever.h"

namespace {

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS]" << name << '\n';
        return;
    }

    std::cout << "[FAIL]" << name << '\n';
    failed_count++;
}

class FakeTextRequester : public TextRequester {
public:
    std::unordered_map<std::string, BackendResult> responses;

    int call_count = 0;
    std::string last_endpoint;
    std::string last_text;
    int last_timeout_ms = 0;

    BackendResult request(
        const std::string& endpoint,
        const std::string& text,
        int timeout_ms
    ) override {
        call_count++;

        last_endpoint = endpoint;
        last_text = text;
        last_timeout_ms = timeout_ms;

        const auto found = responses.find(endpoint);

        if (found == responses.end()) {
            return BackendResult::failure("no fake response");
        }

        if (
            endpoint == "tcp://localhost:8899"
            && found != responses.end()
            && found->second.ok
        ) {
            const auto request = (
                nlohmann::json::parse(text)
            );

            const nlohmann::json response = {
                {"version", 1},
                {"type", "generation_result"},
                {"ok", true},
                {
                    "request_id",
                    request.value(
                        "request_id",
                        ""
                    )
                },
                {"answer", "LLM回答"},
                {"backend", "mock"},
                {"error", ""},
                {"elapsed_ms", 1.0},
                {"finished", true}
            };

            return BackendResult::success(response.dump());
        }

        return found->second;
    }
};

void testLocalRag() {
    Bm25Retriever bm25_retriever("vector_db/chunks.json");

    expectTrue(
        bm25_retriever.loadKnowledgeBase(),
        "load local knowledge base"
    );

    FakeTextRequester requester;

    EdgeResponseBackend backend(
        bm25_retriever,
        requester,
        "local",
        "tcp://localhost:5555",
        3000,
        "tcp://localhost:8899",
        30000,
        3
    );

    const BackendResult result = backend.searchRag("蓝牙怎么连接");

    expectTrue(result.ok, "local RAG succeeds");

    expectTrue(result.text.find("蓝牙") != std::string::npos, "local RAG returns vehicle content");

    expectTrue(requester.call_count == 0, "local RAG skips ZeroMQ");
}

void testZmqRag() {
    Bm25Retriever bm25_retriever("vector_db/chunks.json");

    FakeTextRequester requester;

    requester.responses[
        "tcp://localhost:5556"
    ] = BackendResult::success(
        R"({
            "ok": true,
            "answer": "远程RAG检索内容",
            "generated_answer": "不应该使用的LLM回答"
        })"
    );

    EdgeResponseBackend backend(
        bm25_retriever,
        requester,
        "python_zmq",
        "tcp://localhost:5556",
        4000,
        "tcp://localhost:8899",
        30000,
        3
    );

    const BackendResult result = (backend.searchRag("胎压是多少"));

    expectTrue(result.ok, "ZMQ RAG succeeds");
    expectTrue(result.text == "远程RAG检索内容", "ZMQ RAG prefers retrieved answer");
    expectTrue(requester.last_endpoint == "tcp://localhost:5556", "ZMQ RAG uses configured endpoint");
    expectTrue(requester.last_timeout_ms == 4000, "ZMQ RAG uses configured timeout");
}

void testZmqLlm() {
    Bm25Retriever bm25_retriever("vector_db/chunks.json");

    FakeTextRequester requester;

    requester.responses["tcp://localhost:8899"] = BackendResult::success("{}");

    EdgeResponseBackend backend(
        bm25_retriever,
        requester,
        "local",
        "tcp://localhost:5555",
        3000,
        "tcp://localhost:8899",
        30000,
        3
    );

    const BackendResult result = backend.generateLlm(
        "介绍一下自己"
    );

    expectTrue(result.ok, "ZMQ LLM succeeds");
    expectTrue(result.text == "LLM回答", "ZMQ LLM parses answer");
    expectTrue(requester.last_endpoint == "tcp://localhost:8899", "LLM uses independent endpoint");
    expectTrue(requester.last_timeout_ms == 30000, "LLM uses configured timeout");

    const auto request_json = nlohmann::json::parse(
        requester.last_text
    );

    expectTrue(
        request_json["version"] == 1,
        "LLM sends protocol version"
    );
    expectTrue(
        request_json["type"] == "generate",
        "LLM sends structured request"
    );
    expectTrue(
        request_json["prompt"]
            == "介绍一下自己",
        "LLM request contains prompt"
    );
    expectTrue(
        request_json["stream"] == false,
        "LLM request disables streaming"
    );
    expectTrue(
        !request_json["request_id"]
            .get<std::string>()
            .empty(),
        "LLM request contains request_id"
    );
}

void testRequesterFailure() {
    Bm25Retriever bm25_retriever("vector_db/chunks.json");

    FakeTextRequester requester;

    requester.responses["tcp://localhost:8899"] = BackendResult::failure("timeout");

    EdgeResponseBackend backend(
        bm25_retriever,
        requester,
        "local",
        "tcp://localhost:5555",
        3000,
        "tcp://localhost:8899",
        30000,
        3
    );

    const BackendResult result = backend.generateLlm("你好");

    expectTrue(!result.ok, "LLM failure is propagated");
    expectTrue(result.error == "timeout", "LLM failure keeps reason");
}

}   //namespace

int main() {
    testLocalRag();
    testRequesterFailure();
    testZmqLlm();
    testZmqRag();

    if (failed_count == 0) {
        std::cout
            << "\nAll edge response backend "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
