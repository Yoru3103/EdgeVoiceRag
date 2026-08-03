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
#include <vector>

#include <nlohmann/json.hpp>

#include "bm25_retriever.h"

using Json = nlohmann::json;

namespace {

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
        evaluation_case.category = item.at("id").get<std::string>();
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
    int top_k
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

    metrics_json["positive_cases"] =
        quality.positive_case_count;

    metrics_json["top1_accuracy"] =
        top1_accuracy;

    metrics_json[
        "recall_at_" + std::to_string(top_k)
    ] = recall_at_k;

    metrics_json[
        "mrr_at_" + std::to_string(top_k)
    ] = mean_reciprocal_rank;

    metrics_json["negative_cases"] =
        quality.negative_case_count;

    metrics_json[
        "negative_rejection_accuracy"
    ] = rejection_accuracy;

    metrics_json["latency_mean_us"] =
        mean_latency;

    metrics_json["latency_p50_us"] =
        p50_latency;

    metrics_json["latency_p95_us"] =
        p95_latency;

    metrics_json["latency_max_us"] =
        maximum_latency;

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
    if (argc < 3 || argc > 4) {
        std::cerr
            << "Usage:\n  "
            << argv[0]
            << " <chunks.json>"
            << " <retrieval_cases.json>"
            << " [runs]\n";

        return 1;
    }

    const std::string chunks_path =
        argv[1];

    const std::string cases_path =
        argv[2];

    int runs = 100;

    try {
        if (argc == 4) {
            runs = std::stoi(argv[3]);
        }

        if (runs <= 0) {
            throw std::invalid_argument(
                "runs must be positive"
            );
        }

        Bm25Retriever retriever(
            chunks_path
        );

        if (!retriever.loadKnowledgeBase()) {
            throw std::runtime_error(
                "failed to load BM25 knowledge base: " +
                chunks_path
            );
        }

        const std::vector<EvaluationCase> cases =
            loadEvaluationCases(
                cases_path
            );

        constexpr int top_k = 3;

        std::cout
            << "Retriever: BM25\n"
            << "Documents: "
            << retriever.documentCount()
            << '\n'
            << "Vocabulary: "
            << retriever.vocabularySize()
            << '\n'
            << "Average document length: "
            << retriever.averageDocumentLength()
            << '\n'
            << "Evaluation cases: "
            << cases.size()
            << '\n'
            << "Top-K: "
            << top_k
            << '\n'
            << "Latency runs: "
            << runs
            << '\n';

        const QualityMetrics quality =
            evaluateQuality(
                retriever,
                cases,
                top_k
            );

        std::vector<double> latency_samples =
            measureLatencyMicroseconds(
                retriever,
                cases,
                top_k,
                runs
            );

        printSummary(
            quality,
            std::move(latency_samples),
            top_k
        );

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "retrieval benchmark failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
