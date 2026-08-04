#include "hybrid_retriever.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using edge_voice_rag::HybridRetriever;
using edge_voice_rag::HybridRetrieverConfig;

class StubRetriever final : public Retriever {
public:
    explicit StubRetriever(std::vector<RetrievalResult> results)
        : results_(std::move(results)) {}

    std::vector<RetrievalResult> searchTopK(
        const std::string& query,
        int top_k) const override {

        ++call_count_;
        last_top_k_ = top_k;

        if (query.empty() || top_k <= 0) {
            return {};
        }

        std::vector<RetrievalResult> result = results_;

        if (result.size() > static_cast<std::size_t>(top_k)) {
            result.resize(static_cast<std::size_t>(top_k));
        }

        return result;
    }

    int callCount() const {
        return call_count_;
    }

    int lastTopK() const {
        return last_top_k_;
    }

private:
    std::vector<RetrievalResult> results_;
    mutable int call_count_ = 0;
    mutable int last_top_k_ = 0;
};

RetrievalResult makeResult(
    int chunk_id,
    const std::string& title,
    float sparse_score,
    float dense_score) {

    RetrievalResult result;
    result.chunk.chunk_id = chunk_id;
    result.chunk.title = title;
    result.chunk.text = title + " content";
    result.sparse_score = sparse_score;
    result.dense_score = dense_score;
    return result;
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testFusionRanking() {
    StubRetriever sparse({
        makeResult(1, "A", 10.0F, 0.0F),
        makeResult(2, "B", 8.0F, 0.0F),
        makeResult(3, "C", 6.0F, 0.0F),
    });

    StubRetriever dense({
        makeResult(2, "B", 0.0F, 0.90F),
        makeResult(3, "C", 0.0F, 0.80F),
        makeResult(1, "A", 0.0F, 0.70F),
    });

    HybridRetrieverConfig config;
    config.rrf_k = 60.0F;
    config.candidate_top_k = 3;

    HybridRetriever retriever(sparse, dense, config);
    const auto results = retriever.searchTopK("test query", 3);

    expect(results.size() == 3, "expected three fused results");

    // B 在 BM25 中排名第二，在 Dense 中排名第一，融合后应排名第一。
    expect(results[0].chunk.chunk_id == 2,
           "chunk B should be the first fused result");

    expect(results[0].sparse_score == 8.0F,
           "sparse score should be preserved");

    expect(results[0].dense_score == 0.90F,
           "dense score should be preserved");

    const float expected_score =
        1.0F / 62.0F + 1.0F / 61.0F;

    expect(std::fabs(results[0].final_score - expected_score) < 1e-6F,
           "unexpected RRF score");
}

void testSingleRetrieverFallback() {
    StubRetriever sparse({
        makeResult(10, "Sparse only", 5.0F, 0.0F),
    });

    StubRetriever dense({});

    HybridRetriever retriever(sparse, dense);
    const auto results = retriever.searchTopK("fallback query", 1);

    expect(results.size() == 1,
           "hybrid retriever should support dense fallback");

    expect(results[0].chunk.chunk_id == 10,
           "sparse result should remain available");

    expect(results[0].sparse_score == 5.0F,
           "fallback should preserve sparse score");

    expect(results[0].dense_score == 0.0F,
           "missing dense score should remain zero");
}

void testCandidateDepth() {
    StubRetriever sparse({
        makeResult(1, "A", 1.0F, 0.0F),
    });

    StubRetriever dense({
        makeResult(2, "B", 0.0F, 0.5F),
    });

    HybridRetrieverConfig config;
    config.candidate_top_k = 5;

    HybridRetriever retriever(sparse, dense, config);
    const auto results = retriever.searchTopK("query", 1);

    expect(results.size() == 1, "expected one final result");
    expect(sparse.lastTopK() == 5,
           "sparse retriever should receive candidate_top_k");
    expect(dense.lastTopK() == 5,
           "dense retriever should receive candidate_top_k");
}

void testEmptyQuery() {
    StubRetriever sparse({});
    StubRetriever dense({});

    HybridRetriever retriever(sparse, dense);
    const auto results = retriever.searchTopK("", 3);

    expect(results.empty(), "empty query should return no results");
    expect(sparse.callCount() == 0,
           "empty query should not call sparse retriever");
    expect(dense.callCount() == 0,
           "empty query should not call dense retriever");
}

void testDeterministicTieBreaking() {
    StubRetriever sparse({
        makeResult(20, "Sparse result", 1.0F, 0.0F),
    });

    StubRetriever dense({
        makeResult(10, "Dense result", 0.0F, 0.5F),
    });

    HybridRetriever retriever(sparse, dense);
    const auto results = retriever.searchTopK("query", 2);

    expect(results.size() == 2, "expected two results");
    expect(results[0].chunk.chunk_id == 10,
           "equal scores should be ordered by chunk_id");
    expect(results[1].chunk.chunk_id == 20,
           "equal scores should have deterministic order");
}

void testInvalidConfig() {
    StubRetriever sparse({});
    StubRetriever dense({});

    bool thrown = false;

    try {
        HybridRetrieverConfig config;
        config.rrf_k = 0.0F;
        HybridRetriever retriever(sparse, dense, config);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "zero rrf_k should be rejected");

    thrown = false;

    try {
        HybridRetrieverConfig config;
        config.sparse_weight = 0.0F;
        config.dense_weight = 0.0F;
        HybridRetriever retriever(sparse, dense, config);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "zero weights should be rejected");

    thrown = false;

    try {
        HybridRetrieverConfig config;
        config.candidate_top_k = 0;
        HybridRetriever retriever(sparse, dense, config);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "zero candidate_top_k should be rejected");
}

}  // namespace

int main() {
    try {
        testFusionRanking();
        testSingleRetrieverFallback();
        testCandidateDepth();
        testEmptyQuery();
        testDeterministicTieBreaking();
        testInvalidConfig();

        std::cout << "hybrid_retriever_tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hybrid_retriever_tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
