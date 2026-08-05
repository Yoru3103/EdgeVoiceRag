#include "board_app_config.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " <board-config>\n";

        return 1;
    }

    try {
        const BoardAppConfig config =
            BoardAppConfig::load(argv[1]);

        expect(
            config.retrieval_backend == "hybrid",
            "expected hybrid retrieval backend"
        );

        expect(
            config.relevance_filter_enabled,
            "relevance filter should be enabled"
        );

        expect(
            std::fabs(
                config.relevance_minimum_sparse_score
                - 6.0F
            ) < 0.0001F,
            "unexpected sparse threshold"
        );

        expect(
            std::fabs(
                config.relevance_minimum_dense_similarity
                - 0.40F
            ) < 0.0001F,
            "unexpected dense threshold"
        );

        expect(
            config.relevance_candidate_top_k == 20,
            "unexpected relevance candidate_top_k"
        );

        std::cout
            << "board_app_config_tests passed\n";

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "board_app_config_tests failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
