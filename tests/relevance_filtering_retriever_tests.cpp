#include "relevance_filtering_retriever.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class StubRetriever final : public Retriever {
public:
    explicit StubRetriever(
        std::vector<RetrievalResult> results
    )
        : results_(std::move(results)) {
    }

    std::vector<RetrievalResult> searchTopK(
        const std::string& query,
        int top_k
    ) const override {
        ++call_count_;
        last_top_k_ = top_k;

        if (query.empty() || top_k <= 0) {
            return {};
        }

        std::vector<RetrievalResult> results =
            results_;

        if (
            results.size()
            > static_cast<std::size_t>(top_k)
        ) {
            results.resize(
                static_cast<std::size_t>(top_k)
            );
        }

        return results;
    }

    int callCount() const noexcept {
        return call_count_;
    }

    int lastTopK() const noexcept {
        return last_top_k_;
    }

private:
    std::vector<RetrievalResult> results_;

    mutable int call_count_ = 0;
    mutable int last_top_k_ = 0;
};

RetrievalResult makeResult(
    int chunk_id,
    float sparse_score,
    float dense_score,
    float final_score
) {
    RetrievalResult result;

    result.chunk.chunk_id = chunk_id;
    result.chunk.title =
        "chunk-" + std::to_string(chunk_id);
    result.chunk.text =
        "content-" + std::to_string(chunk_id);

    result.sparse_score = sparse_score;
    result.dense_score = dense_score;
    result.final_score = final_score;

    return result;
}

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

RelevanceFilteringRetrieverConfig
makeConfig() {
    RelevanceFilteringRetrieverConfig config;
    config.minimum_sparse_score = 6.0F;
    config.minimum_dense_similarity = 0.40F;
    config.candidate_top_k = 5;

    return config;
}

void testSparsePass() {
    StubRetriever inner({
        makeResult(
            1,
            7.0F,
            0.0F,
            7.0F
        )
    });

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    const auto results =
        retriever.searchTopK("query", 1);

    expect(
        results.size() == 1,
        "strong sparse result should pass"
    );

    expect(
        results.front().chunk.chunk_id == 1,
        "unexpected sparse result"
    );
}

void testDensePass() {
    StubRetriever inner({
        makeResult(
            2,
            0.0F,
            0.50F,
            0.50F
        )
    });

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    const auto results =
        retriever.searchTopK("query", 1);

    expect(
        results.size() == 1,
        "strong dense result should pass"
    );

    expect(
        results.front().chunk.chunk_id == 2,
        "unexpected dense result"
    );
}

void testWeakResultRejected() {
    StubRetriever inner({
        makeResult(
            3,
            5.1F,
            0.35F,
            0.03F
        )
    });

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    const auto results =
        retriever.searchTopK("query", 1);

    expect(
        results.empty(),
        "weak result should be rejected"
    );
}

void testLaterCandidateCanPass() {
    StubRetriever inner({
        /*
         * 第1名的 final_score 更高，
         * 但原始检索信号都低于阈值。
         */
        makeResult(
            10,
            5.0F,
            0.35F,
            0.032F
        ),

        /*
         * 第2名通过 Dense 阈值。
         */
        makeResult(
            20,
            0.0F,
            0.45F,
            0.016F
        )
    });

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    const auto results =
        retriever.searchTopK("query", 1);

    expect(
        results.size() == 1,
        "later relevant candidate should pass"
    );

    expect(
        results.front().chunk.chunk_id == 20,
        "filter should skip weak first result"
    );
}

void testScoresArePreserved() {
    StubRetriever inner({
        makeResult(
            30,
            8.0F,
            0.60F,
            0.031F
        )
    });

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    const auto results =
        retriever.searchTopK("query", 1);

    expect(
        results.size() == 1,
        "expected preserved result"
    );

    expect(
        results.front().sparse_score == 8.0F,
        "sparse score should be preserved"
    );

    expect(
        results.front().dense_score == 0.60F,
        "dense score should be preserved"
    );

    expect(
        results.front().final_score == 0.031F,
        "final score should be preserved"
    );
}

void testCandidateDepth() {
    StubRetriever inner({
        makeResult(
            1,
            7.0F,
            0.0F,
            7.0F
        )
    });

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    static_cast<void>(
        retriever.searchTopK("query", 2)
    );

    expect(
        inner.lastTopK() == 5,
        "inner retriever should receive "
        "candidate_top_k"
    );
}

void testInvalidInputDoesNotCallInner() {
    StubRetriever inner({});

    RelevanceFilteringRetriever retriever(
        inner,
        makeConfig()
    );

    expect(
        retriever.searchTopK("", 3).empty(),
        "empty query should return empty"
    );

    expect(
        retriever.searchTopK("query", 0).empty(),
        "zero top_k should return empty"
    );

    expect(
        inner.callCount() == 0,
        "invalid input should not call inner"
    );
}

void testInvalidConfig() {
    StubRetriever inner({});

    bool thrown = false;

    try {
        RelevanceFilteringRetrieverConfig config =
            makeConfig();

        config.minimum_sparse_score = 0.0F;

        RelevanceFilteringRetriever retriever(
            inner,
            config
        );
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(
        thrown,
        "zero sparse threshold should fail"
    );

    thrown = false;

    try {
        RelevanceFilteringRetrieverConfig config =
            makeConfig();

        config.minimum_dense_similarity = 1.1F;

        RelevanceFilteringRetriever retriever(
            inner,
            config
        );
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(
        thrown,
        "dense threshold above one should fail"
    );

    thrown = false;

    try {
        RelevanceFilteringRetrieverConfig config =
            makeConfig();

        config.candidate_top_k = 0;

        RelevanceFilteringRetriever retriever(
            inner,
            config
        );
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(
        thrown,
        "zero candidate_top_k should fail"
    );
}

}  // namespace

int main() {
    try {
        testSparsePass();
        testDensePass();
        testWeakResultRejected();
        testLaterCandidateCanPass();
        testScoresArePreserved();
        testCandidateDepth();
        testInvalidInputDoesNotCallInner();
        testInvalidConfig();

        std::cout
            << "relevance_filtering_retriever_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "relevance_filtering_retriever_tests failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
