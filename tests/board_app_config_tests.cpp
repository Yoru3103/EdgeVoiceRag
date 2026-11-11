#include "board_app_config.h"
#include "board_tts_config.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

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

        expect(
            std::fabs(
                config.response_direct_minimum_sparse_score
                - 8.0F
            ) < 0.0001F,
            "unexpected direct RAG sparse threshold"
        );

        expect(
            std::fabs(
                config.response_direct_minimum_dense_similarity
                - 0.55F
            ) < 0.0001F,
            "unexpected direct RAG dense threshold"
        );

        expect(config.tts_model_file == "model.int8.onnx" && config.tts_speaker_id == 0,
               "legacy config should retain original TTS model and speaker defaults");
        std::ifstream original(argv[1]);
        const std::string base((std::istreambuf_iterator<char>(original)), {});
        char pattern[] = "/tmp/edge-tts-config-XXXXXX";
        const int fd = ::mkstemp(pattern);
        expect(fd >= 0, "create isolated config fixture");
        ::close(fd);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
        } cleanup{pattern};
        auto loadExtra = [&](const std::string& extra) {
            std::ofstream out(cleanup.path);
            out << base << "\n" << extra << "\n";
            out.close();
            return BoardAppConfig::load(cleanup.path.string());
        };
        expect(config.barge_in_enabled, "legacy config keeps barge-in enabled");
        expect(!loadExtra("barge_in_enabled=false").barge_in_enabled,
               "explicit false disables barge-in");
        expect(loadExtra("barge_in_enabled=1").barge_in_enabled,
               "explicit one enables barge-in");
        const auto fast = loadExtra(
            "tts_model_dir=assets/tts-fast\ntts_model_file=model.onnx\n"
            "tts_speaker_id=66\ntts_num_threads=4"
        );
        const auto mapped = makeBoardTtsConfig(fast);
        expect(mapped.model_path == "assets/tts-fast/model.onnx"
                   && mapped.speaker_id == 66 && mapped.num_threads == 4,
               "TTS config maps selected model/speaker/threads to every entry");
        expect(mapped.lexicon_path == "assets/tts-fast/lexicon.txt"
                   && mapped.tokens_path == "assets/tts-fast/tokens.txt"
                   && mapped.rule_fsts.size() == 2 && mapped.provider == "cpu"
                   && mapped.speed == 1.0F,
               "TTS voice resources and speed remain consistent");
        for (const std::string extra : {
            "tts_model_file=", "tts_model_file=../model.onnx",
            "tts_model_file=/tmp/model.onnx", "tts_model_file=dir/model.onnx",
            "tts_model_file=C:\\model.onnx", "tts_speaker_id=-1", "barge_in_enabled=off"
        }) {
            bool rejected = false;
            try { (void)loadExtra(extra); }
            catch (const std::exception&) { rejected = true; }
            expect(rejected, "invalid TTS selection must fail: " + extra);
        }

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
