#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "retriever_runtime.h"

using Json = nlohmann::json;

namespace {

struct BenchmarkOptions {
    std::string backend;
    std::string chunks_path;
    std::string cases_path;

    int top_k = 3;
    int runs = 10;

    std::string model_dir;
    std::string index_dir;
};

int parsePositiveInt(
    const std::string& text,
    const std::string& argument_name
) {
    std::size_t parsed_characters = 0;

    int value = 0;

    try {
        value = std::stoi(
            text,
            &parsed_characters
        );
    } catch (const std::exception&) {
        throw std::invalid_argument(
            argument_name
            + " must be a positive integer"
        );
    }

    if (
        parsed_characters != text.size()
        || value <= 0
    ) {
        throw std::invalid_argument(
            argument_name
            + " must be a positive integer"
        );
    }

    return value;
}

BenchmarkOptions parseOptions(
    int argc,
    char* argv[]
) {
    if (argc < 2) {
        throw std::invalid_argument(
            "retrieval backend is missing"
        );
    }

    BenchmarkOptions options;
    options.backend = argv[1];

    if (
        options.backend != "bm25"
        && options.backend != "dense"
        && options.backend != "hybrid"
    ) {
        throw std::invalid_argument(
            "backend must be bm25, dense or hybrid"
        );
    }

    const bool requires_dense_files =
        options.backend == "dense"
        || options.backend == "hybrid";

    const int expected_argument_count =
        requires_dense_files ? 8 : 6;

    if (argc != expected_argument_count) {
        throw std::invalid_argument(
            requires_dense_files
                ? "dense/hybrid requires model_dir and index_dir"
                : "bm25 does not require model_dir or index_dir"
        );
    }

    options.chunks_path = argv[2];
    options.cases_path = argv[3];

    options.top_k = parsePositiveInt(
        argv[4],
        "top_k"
    );

    options.runs = parsePositiveInt(
        argv[5],
        "runs"
    );

    if (requires_dense_files) {
        options.model_dir = argv[6];
        options.index_dir = argv[7];
    }

    return options;
}

RetrieverRuntimeConfig makeRuntimeConfig(
    const BenchmarkOptions& options
) {
    RetrieverRuntimeConfig config;

    config.backend = options.backend;
    config.knowledge_path = options.chunks_path;

    config.dense_minimum_similarity = -1.0F;

    config.hybrid_rrf_k = 60.0F;
    config.hybrid_sparse_weight = 1.0F;
    config.hybrid_dense_weight = 1.0F;
    config.hybrid_candidate_top_k = 20;

    if (
        options.backend == "dense"
        || options.backend == "hybrid"
    ) {
        config.bge_model_path =
            options.model_dir + "/model.onnx";

        config.bge_tokenizer_path =
            options.model_dir + "/tokenizer.json";

        config.dense_index_metadata_path =
            options.index_dir + "/index_meta.json";

        config.dense_embeddings_path =
            options.index_dir + "/embeddings.f32";
    }

    return config;
}

struct EvaluationCase {
    std::string id;
    std::string category;
    std::string query;
    std::vector<int> relevant_chunk_ids;
};

struct CategoryMetrics {
    std::size_t case_count = 0;
    std::size_t top1_hits = 0;

    double recall_sum = 0.0;
    double reciprocal_rank_sum = 0.0;
};

struct QualityMetrics {
    std::size_t positive_case_count = 0;
    std::size_t top1_hits = 0;

    double recall_at_k_sum = 0.0;
    double reciprocal_rank_sum = 0.0;

    std::size_t negative_case_count = 0;
    std::size_t rejected_negative_count = 0;

    std::map<std::string, CategoryMetrics> category_metrics;
};

std::vector<EvaluationCase> loadEvaluationCases(const std::string& path) {
    std::ifstream input(path);

    if (!input.is_open()) {
        throw std::runtime_error(
            "failed to open evaluation cases: " +
            path
        );
    }

    Json root;
    input >> root;

    if (!root.is_array()) {
        throw std::runtime_error(
            "evaluation cases must contain a JSON array"
        );
    }

    std::vector<EvaluationCase> cases;
    std::unordered_set<std::string> loaded_ids;

    cases.reserve(root.size());

    for (const Json& item : root) {
        if (
            !item.is_object() ||
            !item.contains("id") ||
            !item.contains("category") ||
            !item.contains("query") ||
            !item.contains("relevant_chunk_ids")
        ) {
            throw std::runtime_error(
                "invalid evaluation case"
            );
        }

        EvaluationCase evaluation_case;
        evaluation_case.id = item.at("id").get<std::string>();
        evaluation_case.category = item.at("category").get<std::string>();
        evaluation_case.query = item.at("query").get<std::string>();
        evaluation_case.relevant_chunk_ids = item.at("relevant_chunk_ids").get<std::vector<int>>();

        if (
            evaluation_case.id.empty() ||
            evaluation_case.query.empty() ||
            evaluation_case.category.empty()
        ) {
            throw std::runtime_error(
                "evaluation case contains empty field"
            );
        }

        const bool inserted = loaded_ids.insert(evaluation_case.id).second;

        if (!inserted) {
            throw std::runtime_error(
                "duplicate evaluation case id: " +
                evaluation_case.id
            );
        }

        cases.push_back(evaluation_case);
    }

    if (cases.empty()) {
        throw std::runtime_error(
            "no evaluation cases loaded"
        );
    }

    return cases;
}

bool containsRelevantChunk(
    const std::vector<int>& relevant_chunk_ids,
    int chunk_id
) {
    return std::find(
        relevant_chunk_ids.begin(),
        relevant_chunk_ids.end(),
        chunk_id
    ) != relevant_chunk_ids.end();
}

std::size_t countRetrievedRelevantChunks(
    const std::vector<RetrievalResult>& results,
    const std::vector<int>& relevant_chunk_ids
) {
    std::unordered_set<int> retrieved_relevant_ids;

    for (const RetrievalResult& result : results) {
        if (containsRelevantChunk(relevant_chunk_ids, result.chunk.chunk_id)) {
            retrieved_relevant_ids.insert(result.chunk.chunk_id);
        }
    }

    return retrieved_relevant_ids.size();
}

double findReciprocalRank(
    const std::vector<RetrievalResult>& results,
    const std::vector<int>& relevant_chunk_ids
) {
    for (std::size_t index = 0; index < results.size(); index++) {
        if (containsRelevantChunk(relevant_chunk_ids, results[index].chunk.chunk_id)) {
            return 1.0 / static_cast<double>(index + 1);
        }
    }

    return 0.0;
}

double safeRatio(double numerator, double denominator) {
    if (denominator <= 0.0) {
        return 0.0;
    }

    return numerator / denominator;
}

QualityMetrics evaluateQuality(
    const Retriever& retriever,
    const std::vector<EvaluationCase>& cases,
    int top_k
) {
    QualityMetrics metrics;

    std::cout << "\nPer-case results\n";
    std::cout << "----------------\n";

    for (const EvaluationCase& evaluation_case : cases) {
        const std::vector<RetrievalResult> results = retriever.searchTopK(evaluation_case.query, top_k);

        if (evaluation_case.relevant_chunk_ids.empty()) {
            metrics.negative_case_count++;

            const bool rejected = results.empty();

            if (rejected) {
                metrics.rejected_negative_count++;
            }

            std::cout
                << (rejected
                    ? "[REJECT] "
                    : "[FALSE POSITIVE] ")
                << evaluation_case.id
                << " | query="
                << evaluation_case.query;

            if (!results.empty()) {
                std::cout
                << " | returned_chunk="
                << results.front().chunk.chunk_id
                << " | title="
                << results.front().chunk.title
                << " | score="
                << results.front().final_score;
            }

            std::cout << '\n';
            continue;
        }

        metrics.positive_case_count++;

        CategoryMetrics& category = metrics.category_metrics[evaluation_case.category];

        category.case_count++;

        const bool top1_hit = 
            !results.empty() && 
            containsRelevantChunk(
                evaluation_case.relevant_chunk_ids,
                results.front().chunk.chunk_id
            );

        if (top1_hit) {
            metrics.top1_hits++;
            category.top1_hits++;
        }

        const std::size_t retrieved_relevant  = 
        countRetrievedRelevantChunks(
            results,
            evaluation_case.relevant_chunk_ids
        );

        const double recall = 
            safeRatio(static_cast<double>(retrieved_relevant), 
                static_cast<double>(evaluation_case.relevant_chunk_ids.size())
            );

        const double reciprocal_rank = 
            findReciprocalRank(
                results,
                evaluation_case.relevant_chunk_ids
            );

        metrics.recall_at_k_sum += recall;
        metrics.reciprocal_rank_sum += reciprocal_rank;

        category.recall_sum += recall;
        category.reciprocal_rank_sum += reciprocal_rank;

        std::string status = "[MISS]";

        if (top1_hit) {
            status = "[TOP1]";
        } else if (recall > 0.0) {
            status = "[HIT@K]";
        }

        std::cout
            << status
            << ' '
            << evaluation_case.id
            << " | query="
            << evaluation_case.query;

        if (!results.empty()) {
            std::cout
                << " | returned_chunk="
                << results.front().chunk.chunk_id
                << " | title="
                << results.front().chunk.title
                << " | score="
                << results.front().final_score;
        } else {
            std::cout << " | no result";
        }

        std::cout << '\n';
    }

    return metrics;
}

double percentile(
    const std::vector<double>& sorted_values,
    double probability
) {
    if (sorted_values.empty()) {
        return 0.0;
    }

    const double raw_position = 
        std::ceil(probability * static_cast<double>(sorted_values.size()));

    std::size_t index = 0;

    if (raw_position > 1.0) {
        index = static_cast<std::size_t>(raw_position - 1);
    }

    index = std::min(index, sorted_values.size() - 1);

    return sorted_values[index];
}

std::vector<double> measureLatencyMicroseconds(
    const Retriever& retriever,
    const std::vector<EvaluationCase>& cases,
    int top_k,
    int runs
) {
    /*
     * 预热：
     * 减少首次运行的内存分配和缓存影响。
     */
    for (const EvaluationCase& evaluation_case : cases) {
        static_cast<void>(retriever.searchTopK(evaluation_case.query, top_k));
    }

    std::vector<double> latency_samples;

    latency_samples.reserve(
        static_cast<std::size_t>(runs) * cases.size()
    );

    /*
     * checksum确保编译器不能简单认为查询结果无用。
     */
    std::size_t checksum = 0;

    for (int run = 0; run < runs; run++) {
        for (const EvaluationCase& evaluation_case : cases) {
            const auto start = std::chrono::steady_clock::now();

            const auto results = retriever.searchTopK(evaluation_case.query, top_k);

            const auto end = std::chrono::steady_clock::now();

            const double elapsed_microseconds = std::chrono::duration<double, std::micro>(
                end - start
            ).count();

            latency_samples.push_back(elapsed_microseconds);

            checksum += results.size();
        }
    }

    std::cout
        << "\nLatency checksum: "
        << checksum
        << '\n';

    return latency_samples;
}

void printSummary(
    const QualityMetrics& quality,
    std::vector<double> latency_samples,
    const std::string& backend,
    int top_k,
    int runs,
    double load_time_ms
) {
    const double positive_count =
        static_cast<double>(
            quality.positive_case_count
        );

    const double negative_count =
        static_cast<double>(
            quality.negative_case_count
        );

    // 正确文档是否排在第一位
    const double top1_accuracy =
        safeRatio(
            static_cast<double>(
                quality.top1_hits
            ),
            positive_count
        );
    
    // 正确文档是否出现在前k条结果中
    const double recall_at_k =
        safeRatio(
            quality.recall_at_k_sum,
            positive_count
        );

    // 不仅判断是否命中，还考虑正确结果的位置
    const double mean_reciprocal_rank =
        safeRatio(
            quality.reciprocal_rank_sum,
            positive_count
        );

    // 无关问题是否返回空结果
    const double rejection_accuracy =
        safeRatio(
            static_cast<double>(
                quality.rejected_negative_count
            ),
            negative_count
        );

    std::sort(
        latency_samples.begin(),
        latency_samples.end()
    );

    // 延迟指标
    const double mean_latency =
        latency_samples.empty()
            ? 0.0
            : std::accumulate(
                latency_samples.begin(),
                latency_samples.end(),
                0.0
            ) /
            static_cast<double>(
                latency_samples.size()
            );

    const double p50_latency =
        percentile(
            latency_samples,
            0.50
        );

    const double p95_latency =
        percentile(
            latency_samples,
            0.95
        );

    const double maximum_latency =
        latency_samples.empty()
            ? 0.0
            : latency_samples.back();

    std::cout
        << "\nBenchmark configuration\n"
        << "-----------------------\n"
        << "backend: "
        << backend
        << '\n'
        << "top_k: "
        << top_k
        << '\n'
        << "runs: "
        << runs
        << '\n'
        << "load_time_ms: "
        << load_time_ms
        << '\n';

    std::cout
        << "\nOverall quality\n"
        << "---------------\n"
        << std::fixed
        << std::setprecision(4)
        << "positive_cases: "
        << quality.positive_case_count
        << '\n'
        << "top1_accuracy: "
        << top1_accuracy
        << '\n'
        << "recall@"
        << top_k
        << ": "
        << recall_at_k
        << '\n'
        << "mrr@"
        << top_k
        << ": "
        << mean_reciprocal_rank
        << '\n'
        << "negative_cases: "
        << quality.negative_case_count
        << '\n'
        << "negative_rejection_accuracy: "
        << rejection_accuracy
        << '\n';

    std::cout
        << "\nQuality by category\n"
        << "-------------------\n";

    for (
        const auto& [name, category] :
        quality.category_metrics
    ) {
        const double case_count =
            static_cast<double>(
                category.case_count
            );

        std::cout
            << name
            << ": cases="
            << category.case_count
            << ", top1="
            << safeRatio(
                static_cast<double>(
                    category.top1_hits
                ),
                case_count
            )
            << ", recall@"
            << top_k
            << '='
            << safeRatio(
                category.recall_sum,
                case_count
            )
            << ", mrr="
            << safeRatio(
                category.reciprocal_rank_sum,
                case_count
            )
            << '\n';
    }

    std::cout
        << "\nLatency\n"
        << "-------\n"
        << "samples: "
        << latency_samples.size()
        << '\n'
        << "mean_us: "
        << mean_latency
        << '\n'
        << "p50_us: "
        << p50_latency
        << '\n'
        << "p95_us: "
        << p95_latency
        << '\n'
        << "max_us: "
        << maximum_latency
        << '\n';

    /*
     * 最后一行输出机器可读JSON。
     * 后续可用脚本比较BM25与HybridRetriever。
     */
    Json metrics_json;

    metrics_json["positive_cases"] = quality.positive_case_count;
    metrics_json["top1_accuracy"] = top1_accuracy;
    metrics_json["recall_at_" + std::to_string(top_k)] = recall_at_k;
    metrics_json["mrr_at_" + std::to_string(top_k)] = mean_reciprocal_rank;
    metrics_json["negative_cases"] = quality.negative_case_count;
    metrics_json["negative_rejection_accuracy"] = rejection_accuracy;
    metrics_json["latency_mean_us"] = mean_latency;
    metrics_json["latency_p50_us"] = p50_latency;
    metrics_json["latency_p95_us"] = p95_latency;
    metrics_json["latency_max_us"] = maximum_latency;
    metrics_json["backend"] = backend;
    metrics_json["top_k"] = top_k;
    metrics_json["runs"] = runs;
    metrics_json["load_time_ms"] = load_time_ms;

    Json category_json = Json::object();

    for (
        const auto& [name, category] :
        quality.category_metrics
    ) {
        const double case_count =
            static_cast<double>(
                category.case_count
            );

        category_json[name] = {
            {
                "cases",
                category.case_count
            },
            {
                "top1_accuracy",
                safeRatio(
                    static_cast<double>(
                        category.top1_hits
                    ),
                    case_count
                )
            },
            {
                "recall_at_" +
                    std::to_string(top_k),
                safeRatio(
                    category.recall_sum,
                    case_count
                )
            },
            {
                "mrr",
                safeRatio(
                    category.reciprocal_rank_sum,
                    case_count
                )
            }
        };
    }

    metrics_json["categories"] =
        std::move(category_json);

    std::cout
        << "\nMETRICS_JSON "
        << metrics_json.dump()
        << '\n';
}

}

int main(int argc, char* argv[]) {
    try {
        const BenchmarkOptions options =
            parseOptions(argc, argv);

        const std::vector<EvaluationCase> cases =
            loadEvaluationCases(
                options.cases_path
            );

        RetrieverRuntimeConfig runtime_config =
            makeRuntimeConfig(options);

        const auto load_start =
            std::chrono::steady_clock::now();

        RetrieverRuntime runtime(
            std::move(runtime_config)
        );

        if (!runtime.load()) {
            throw std::runtime_error(
                runtime.lastError()
            );
        }

        const auto load_end =
            std::chrono::steady_clock::now();

        const double load_time_ms =
            std::chrono::duration<double, std::milli>(
                load_end - load_start
            ).count();

        Retriever& retriever =
            runtime.retriever();

        std::cout
            << "Retriever: "
            << runtime.backendName()
            << '\n'
            << "Knowledge: "
            << options.chunks_path
            << '\n'
            << "Evaluation cases: "
            << cases.size()
            << '\n'
            << "Top-K: "
            << options.top_k
            << '\n'
            << "Latency runs: "
            << options.runs
            << '\n'
            << "Load time: "
            << load_time_ms
            << " ms\n";

        const QualityMetrics quality =
            evaluateQuality(
                retriever,
                cases,
                options.top_k
            );

        std::vector<double> latency_samples =
            measureLatencyMicroseconds(
                retriever,
                cases,
                options.top_k,
                options.runs
            );

        printSummary(
            quality,
            std::move(latency_samples),
            options.backend,
            options.top_k,
            options.runs,
            load_time_ms
        );

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "Usage:\n"
            << "  "
            << argv[0]
            << " bm25"
            << " <chunks.json>"
            << " <retrieval_cases.json>"
            << " <top_k>"
            << " <runs>\n"
            << "  "
            << argv[0]
            << " dense|hybrid"
            << " <chunks.json>"
            << " <retrieval_cases.json>"
            << " <top_k>"
            << " <runs>"
            << " <model_dir>"
            << " <index_dir>\n\n"
            << "retrieval benchmark failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
