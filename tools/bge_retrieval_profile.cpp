#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include "dense_retriever.h"

namespace {
int integer(const std::string& text, int minimum) {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (used != text.size() || value < minimum) {
        throw std::invalid_argument("invalid numeric argument: " + text);
    }
    return value;
}
struct Case { std::string id; std::string query; };
struct Sample {
    int run;
    std::size_t case_index;
    DenseRetrievalTiming timing;
    std::vector<int> chunk_ids;
};
}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 9) {
        std::cerr << "Usage: bge_retrieval_profile <chunks> <cases> <model_dir> "
                  << "<index_dir> <top_k> <runs> <warmup_passes> <intra_threads>\n";
        return 2;
    }
    try {
        const int top_k = integer(argv[5], 1);
        const int runs = integer(argv[6], 1);
        const int warmup = integer(argv[7], 0);
        const int threads = integer(argv[8], 1);
        std::ifstream input(argv[2]);
        if (!input) { throw std::runtime_error("cannot open cases"); }
        nlohmann::json root;
        input >> root;
        if (!root.is_array() || root.empty()) { throw std::runtime_error("cases must be a nonempty array"); }
        std::vector<Case> cases;
        std::unordered_set<std::string> ids;
        for (const auto& item : root) {
            Case c{item.at("id").get<std::string>(), item.at("query").get<std::string>()};
            if (c.id.empty() || c.query.empty() || !ids.insert(c.id).second) {
                throw std::runtime_error("empty/duplicate case");
            }
            cases.push_back(std::move(c));
        }
        DenseRetrieverConfig config;
        config.knowledge_path = argv[1];
        config.embedder.model_path = std::string(argv[3]) + "/model.onnx";
        config.embedder.tokenizer_path = std::string(argv[3]) + "/tokenizer.json";
        config.embedder.intra_op_threads = threads;
        config.embedder.inter_op_threads = 1;
        config.index_metadata_path = std::string(argv[4]) + "/index_meta.json";
        config.embeddings_path = std::string(argv[4]) + "/embeddings.f32";
        DenseRetriever retriever(config);
        const auto load_start = std::chrono::steady_clock::now();
        if (!retriever.load()) { throw std::runtime_error(retriever.lastError()); }
        const double load_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - load_start).count();
        for (int run = 0; run < warmup; ++run) {
            for (const auto& c : cases) { retriever.searchTopK(c.query, top_k); }
        }
        std::vector<Sample> samples;
        samples.reserve(static_cast<std::size_t>(runs) * cases.size());
        for (int run = 0; run < runs; ++run) {
            for (std::size_t i = 0; i < cases.size(); ++i) {
                Sample sample;
                sample.run = run;
                sample.case_index = i;
                const auto results = retriever.searchTopKProfiled(cases[i].query, top_k, sample.timing);
                if (!sample.timing.observed || !sample.timing.embedding.observed) {
                    throw std::runtime_error("unobserved profile: " + cases[i].id);
                }
                for (const auto& r : results) { sample.chunk_ids.push_back(r.chunk.chunk_id); }
                samples.push_back(std::move(sample));
            }
        }
        // Serialize only after measurement. These are app-stage wall times,
        // not ORT operator traces or hardware utilization.
        std::cout << nlohmann::json{
            {"version",1},{"type","dense_profile_config"},{"load_ms",load_ms},
            {"case_count",cases.size()},{"runs",runs},{"warmup_passes",warmup},
            {"intra_op_threads",threads},{"inter_op_threads",1},{"top_k",top_k},
            {"relevance_filter_enabled",false},{"provider","onnxruntime_cpu"}
        }.dump() << '\n';
        for (const auto& s : samples) {
            const auto& e = s.timing.embedding;
            std::cout << nlohmann::json{
                {"version",1},{"type","dense_profile_sample"},{"run",s.run},
                {"case_id",cases[s.case_index].id},{"query_bytes",cases[s.case_index].query.size()},
                {"token_count",e.token_count},{"chunk_ids",s.chunk_ids},
                {"lock_wait_ms",e.lock_wait_ms},{"prepare_ms",e.prepare_ms},
                {"inference_ms",e.inference_ms},{"postprocess_ms",e.postprocess_ms},
                {"embedding_total_ms",e.total_ms},{"index_search_ms",s.timing.index_search_ms},
                {"mapping_ms",s.timing.mapping_ms},{"total_ms",s.timing.total_ms}
            }.dump() << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "profile failed: " << e.what() << '\n';
        return 1;
    }
}
