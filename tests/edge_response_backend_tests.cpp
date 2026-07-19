#include <iostream>
#include <string>
#include <unordered_map>

#include "edge_response_backend.h"

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

        return found->second;
    }
};

void testLocalRag() {
    RagEngine rag_engine("docs/vehicle_manual.txt");

    expectTrue(
        rag_engine.loadKnowledgeBase(),
        "load local knowledge base"
    );

    FakeTextRequester requester;

    EdgeResponseBackend backend(
        rag_engine,
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
    RagEngine rag_engine("docs/vehicle_manual.txt");

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
        rag_engine,
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
    RagEngine rag_engine("docs/vehicle_manual.txt");

    FakeTextRequester requester;

    requester.responses["tcp://localhost:8899"] = BackendResult::success(R"({"ok":true,"answer":"LLM回答"})");

    EdgeResponseBackend backend(
        rag_engine,
        requester,
        "local",
        "tcp://localhost:5555",
        3000,
        "tcp://localhost:8899",
        30000,
        3
    );

    const BackendResult result = backend.generateLlm("介绍一下自己");

    expectTrue(result.ok, "ZMQ LLM succeeds");
    expectTrue(result.text == "LLM回答", "ZMQ LLM parses answer");
    expectTrue(requester.last_endpoint == "tcp://localhost:8899", "LLM uses independent endpoint");
}

void testRequesterFailure() {
    RagEngine rag_engine("docs/vehicle_manual.txt");

    FakeTextRequester requester;

    requester.responses["tcp://localhost:8899"] = BackendResult::failure("timeout");

    EdgeResponseBackend backend(
        rag_engine,
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
