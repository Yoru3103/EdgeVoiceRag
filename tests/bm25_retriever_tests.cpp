#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bm25_retriever.h"

namespace {

int g_failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& test_name
) {
    if (condition) {
        std::cout
            << "[PASS] "
            << test_name
            << '\n';
    } else {
        std::cerr
            << "[FAIL] "
            << test_name
            << '\n';

        ++g_failed_count;
    }
}

Bm25Retriever createRetriever() {
    Bm25Retriever retriever(
        "vector_db/chunks.json"
    );

    const bool loaded =
        retriever.loadKnowledgeBase();

    expectTrue(
        loaded,
        "BM25: load knowledge base"
    );

    return retriever;
}

void testIndexStatistics() {
    Bm25Retriever retriever =
        createRetriever();

    expectTrue(
        retriever.documentCount() == 8,
        "BM25: document count"
    );

    expectTrue(
        retriever.vocabularySize() > 0,
        "BM25: vocabulary should not be empty"
    );

    expectTrue(
        retriever.averageDocumentLength() > 0.0F,
        "BM25: average document length"
    );
}

void testAirConditionerQuery() {
    Bm25Retriever retriever =
        createRetriever();

    const std::vector<RetrievalResult> results =
        retriever.searchTopK(
            "空调怎么打开",
            3
        );

    expectTrue(
        !results.empty(),
        "BM25: air conditioner query should return result"
    );

    if (!results.empty()) {
        expectTrue(
            results.front().chunk.title ==
                "空调系统",
            "BM25: air conditioner should rank first"
        );

        expectTrue(
            results.front().sparse_score > 0.0F,
            "BM25: sparse score should be positive"
        );

        expectTrue(
            results.front().dense_score == 0.0F,
            "BM25: dense score should remain zero"
        );

        expectTrue(
            std::fabs(
                results.front().final_score -
                results.front().sparse_score
            ) < 0.0001F,
            "BM25: final score should equal sparse score"
        );
    }
}

void testBluetoothParaphrase() {
    Bm25Retriever retriever =
        createRetriever();

    const std::vector<RetrievalResult> results =
        retriever.searchTopK(
            "手机如何和车辆进行配对",
            3
        );

    expectTrue(
        !results.empty(),
        "BM25: bluetooth paraphrase should return result"
    );

    if (!results.empty()) {
        expectTrue(
            results.front().chunk.title ==
                "蓝牙连接",
            "BM25: pairing query should rank bluetooth first"
        );
    }
}

void testTrunkQuery() {
    Bm25Retriever retriever =
        createRetriever();

    const std::vector<RetrievalResult> results =
        retriever.searchTopK(
            "后备箱如何开启",
            3
        );

    expectTrue(
        !results.empty(),
        "BM25: trunk query should return result"
    );

    if (!results.empty()) {
        expectTrue(
            results.front().chunk.title ==
                "后备箱开启",
            "BM25: trunk document should rank first"
        );
    }
}

void testTopKLimit() {
    Bm25Retriever retriever =
        createRetriever();

    const std::vector<RetrievalResult> results =
        retriever.searchTopK(
            "怎么打开和控制车辆功能",
            2
        );

    expectTrue(
        results.size() <= 2,
        "BM25: result count should respect top_k"
    );
}

void testInvalidInput() {
    Bm25Retriever retriever =
        createRetriever();

    expectTrue(
        retriever.searchTopK("", 3).empty(),
        "BM25: empty query"
    );

    expectTrue(
        retriever
            .searchTopK("空调怎么打开", 0)
            .empty(),
        "BM25: zero top_k"
    );

    expectTrue(
        retriever
            .searchTopK("quantum physics", 3)
            .empty(),
        "BM25: unrelated ASCII query"
    );
}

void testMissingKnowledgeBase() {
    Bm25Retriever retriever(
        "vector_db/not-found.json"
    );

    expectTrue(
        !retriever.loadKnowledgeBase(),
        "BM25: missing knowledge base should fail"
    );

    expectTrue(
        retriever
            .searchTopK("空调", 3)
            .empty(),
        "BM25: unloaded retriever should return no result"
    );
}

void testInvalidConfiguration() {
    bool rejected = false;

    try {
        Bm25RetrieverConfig config;
        config.k1 = 0.0F;

        const Bm25Retriever retriever(
            "vector_db/chunks.json",
            config
        );

        static_cast<void>(retriever);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "BM25: invalid k1 should be rejected"
    );
}

void testAirConditionerSemanticQuery() {
    Bm25Retriever retriever =
        createRetriever();

    const auto results =
        retriever.searchTopK(
            "车里太热了，怎样凉快一点",
            3
        );

    expectTrue(
        !results.empty(),
        "BM25: semantic air conditioner "
        "query should return result"
    );

    if (!results.empty()) {
        expectTrue(
            results.front().chunk.chunk_id == 0,
            "BM25: semantic air conditioner "
            "should rank first"
        );

        expectTrue(
            results.front().chunk.text.find(
                "相关表达"
            ) == std::string::npos,
            "BM25: answer text must not "
            "contain aliases"
        );
    }
}

} // namespace

int main() {
    testIndexStatistics();
    testAirConditionerQuery();
    testBluetoothParaphrase();
    testTrunkQuery();
    testTopKLimit();
    testInvalidInput();
    testMissingKnowledgeBase();
    testInvalidConfiguration();
    testAirConditionerSemanticQuery();

    if (g_failed_count == 0) {
        std::cout
            << "All BM25 retriever tests passed."
            << '\n';

        return 0;
    }

    std::cerr
        << g_failed_count
        << " BM25 retriever test(s) failed."
        << '\n';

    return 1;
}
