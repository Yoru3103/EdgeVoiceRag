#include "dense_vector_index.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

using Json = nlohmann::json;
namespace {

bool isLowercaseHexFingerprint(const std::string& value) {
    if (value.size() != 16) {
        return false;
    }

    for (const char character : value) {
        const bool digit = character >= '0' && character <= '9';
        const bool lowercase_hex = character >= 'a' && character <= 'f';

        if (!digit && !lowercase_hex) {
            return false;
        }
    }

    return true;
}

}   // namespace

DenseVectorIndex::DenseVectorIndex(
    std::string metadata_path,
    std::string embeddings_path
)
    : metadata_path_(metadata_path)
    , embeddings_path_(embeddings_path) {}

void DenseVectorIndex::resetData() {
    loaded_ = false;

    rows_ = 0;
    dimension_ = 0;
    max_length_ = 0;

    model_id_.clear();
    pooling_.clear();
    query_instruction_.clear();
    chunks_fingerprint_.clear();

    chunk_ids_.clear();
    embeddings_.clear();
}

bool DenseVectorIndex::fail(const std::string& message) {
    resetData();
    last_error_ = message;

    return false;
}

bool DenseVectorIndex::load() {
    resetData();
    last_error_.clear();

    // 确保小端序（模型是小端序）
    const std::uint16_t endian_marker = 1;

    const auto* endian_bytes = reinterpret_cast<const std::uint8_t*>(&endian_marker);

    if (endian_bytes[0] != 1) {
        return fail(
            "dense vector index requires "
            "a little-endian host"
        );
    }

    if (sizeof(float) != 4 || !std::numeric_limits<float>::is_iec559) {
        return fail(
            "dense vector index requires "
            "32-bit IEEE-754 float"
        );
    }

    std::ifstream metadata_input(metadata_path_);

    if (!metadata_input.is_open()) {
        return fail(
            "failed to open dense index metadata: " +
            metadata_path_
        );
    }

    try {
        Json metadata;
        metadata_input >> metadata;

        if (!metadata.is_object()) {
            return fail(
                "dense index metadata must be an object"
            );
        }

        const std::string format = metadata.at("format").get<std::string>();
        const int version = metadata.at("version").get<int>();
        const std::string dtype = metadata.at("dtype").get<std::string>();
        const bool normalized = metadata.at("normalized").get<bool>();
        const std::string model_id = metadata.at("model_id").get<std::string>();
        const std::string pooling = metadata.at("pooling").get<std::string>();
        const std::string query_instruction = metadata.at("query_instruction").get<std::string>();
        const std::string chunks_fingerprint = metadata.at("chunks_fnv1a64").get<std::string>();
        const int max_length = metadata.at("max_length").get<int>();

        if (
            format != "edge_voice_rag_dense_index"
        ) {
            return fail(
                "unsupported dense index format: " +
                format
            );
        }

        if(version != 2) {
            return fail(
                "unsupported dense index version: "
                + std::to_string(version)
                + "; rebuild the dense index"
            );
        }

        if (dtype != "float32_le") {
            return fail(
                "unsupported dense index dtype: " +
                dtype
            );
        }

        if (!normalized) {
            return fail(
                "dense index vectors must be normalized"
            );
        }

        if (pooling != "cls") {
            return fail(
                "unsupported embedding pooling: " +
                pooling
            );
        }

        if (
            model_id.empty() ||
            query_instruction.empty() ||
            max_length <= 0
        ) {
            return fail(
                "invalid embedding model metadata"
            );
        }
        
        if (!isLowercaseHexFingerprint(chunks_fingerprint)) {
            return fail("invalid chunks_fnv1a64 in dense index metadata");
        }

        const Json& rows_value = metadata.at("rows");

        const Json& dimension_value = metadata.at("dimension");

        if (
            !rows_value.is_number_integer() ||
            !dimension_value.is_number_integer()
        ) {
            return fail(
                "rows and dimension must be integers"
            );
        }

        const std::int64_t parsed_rows = rows_value.get<std::int64_t>();
        const std::int64_t parsed_dimension = dimension_value.get<std::int64_t>();

        if (
            parsed_rows <= 0 ||
            parsed_dimension <= 0
        ) {
            return fail(
                "rows and dimension must be positive"
            );
        }

        const auto rows = static_cast<std::size_t>(parsed_rows);
        const auto dimension = static_cast<std::size_t>(parsed_dimension);

        // 防止溢出
        if (
            rows >
            std::numeric_limits<std::size_t>::max() / dimension
        ) {
            return fail(
                "dense index element count overflow"
            );
        }

        const std::size_t element_count = rows * dimension;

        if (element_count > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
            return fail(
                "dense index byte count overflow"
            );
        }

        const std::size_t expected_bytes = element_count * sizeof(float);
        const Json& chunk_ids_value = metadata.at("chunk_ids");

        if (!chunk_ids_value.is_array()) {
            return fail(
                "chunk_ids must be an array"
            );
        }

        if (
            chunk_ids_value.size() != rows
        ) {
            return fail(
                "chunk_ids count does not match rows"
            );
        }

        std::vector<int> chunk_ids;
        std::unordered_set<int> unique_chunk_ids;

        for (const Json& chunk_id_value : chunk_ids_value) {
            if (!chunk_id_value.is_number_integer()) {
                return fail(
                    "chunk_id must be an integer"
                );
            }

            const int chunk_id = chunk_id_value.get<int>();

            if (chunk_id < 0) {
                return fail(
                    "chunk_id must not be negative"
                );
            }

            const bool inserted =
                unique_chunk_ids.insert(
                    chunk_id
                ).second;

            if (!inserted) {
                return fail(
                    "duplicate chunk_id in dense index"
                );
            }

            chunk_ids.push_back(chunk_id);
        }

        std::ifstream embeddings_input(
            embeddings_path_,
            std::ios::binary |
            std::ios::ate
        );

        if (!embeddings_input.is_open()) {
            return fail(
                "failed to open dense embeddings: " +
                embeddings_path_
            );
        }

        const std::streampos end_position = embeddings_input.tellg();

        if (end_position < 0) {
            return fail(
                "failed to determine embeddings file size"
            );
        }

        const auto actual_bytes = static_cast<std::size_t>(end_position);

        if (actual_bytes != expected_bytes) {
            return fail(
                "dense embeddings size mismatch: "
                "expected=" +
                std::to_string(expected_bytes) +
                ", actual=" +
                std::to_string(actual_bytes)
            );
        }

        embeddings_input.seekg(
            0,
            std::ios::beg
        );

        std::vector<float> embeddings(
            element_count
        );

        embeddings_input.read(
            reinterpret_cast<char*>(
                embeddings.data()
            ),
            static_cast<std::streamsize>(
                expected_bytes
            )
        );

        if (
            !embeddings_input ||
            static_cast<std::size_t>(
                embeddings_input.gcount()
            ) != expected_bytes
        ) {
            return fail(
                "failed to read complete dense embeddings"
            );
        }

        // 验证所有浮点数有效，并验证每行已经归一化。
        constexpr double norm_tolerance = 1.0e-3;

        for (
            std::size_t row = 0;
            row < rows;
            ++row
        ) {
            double squared_norm = 0.0;

            const std::size_t offset =
                row * dimension;

            for (
                std::size_t column = 0;
                column < dimension;
                ++column
            ) {
                const float value =
                    embeddings[
                        offset + column
                    ];

                if (!std::isfinite(value)) {
                    return fail(
                        "dense embeddings contain "
                        "NaN or Inf"
                    );
                }

                squared_norm +=
                    static_cast<double>(value) *
                    static_cast<double>(value);
            }

            const double norm =
                std::sqrt(squared_norm);

            if (
                std::abs(norm - 1.0) >
                norm_tolerance
            ) {
                return fail(
                    "dense embedding row is "
                    "not normalized: row=" +
                    std::to_string(row)
                );
            }
        }

        /*
         * 所有验证完成后才写入成员变量。
         */
        rows_ = rows;
        dimension_ = dimension;
        max_length_ = max_length;

        model_id_ = model_id;
        pooling_ = pooling;
        query_instruction_ = query_instruction;
        chunks_fingerprint_ = chunks_fingerprint;

        chunk_ids_ = std::move(chunk_ids);

        embeddings_ = std::move(embeddings);

        loaded_ = true;
        last_error_.clear();

        return true;
    } catch (const Json::exception& error) {
        return fail(
            "invalid dense index metadata: " + std::string(error.what())
        );
    }
}

std::vector<DenseVectorSearchHit> DenseVectorIndex::searchTopK(
    const std::vector<float>& query_embedding,
    int top_k
) const {
    std::vector<DenseVectorSearchHit> results;

    if (
        !loaded_ ||
        top_k <= 0 ||
        query_embedding.size() != dimension_
    ) {
        return results;
    }

    double query_squared_norm = 0.0;

    for (const float value : query_embedding) {
        if (!std::isfinite(value)) {
            return {};
        }

        query_squared_norm += static_cast<double>(value) * static_cast<double>(value);
    }

    if (query_squared_norm <= 0.0) {
        return results;
    }

    const double query_norm = std::sqrt(query_squared_norm);
    results.reserve(rows_);

    for (std::size_t row = 0; row < rows_; row++) {
        const std::size_t offset = row * dimension_;

        double dot_product = 0.0;

        for (std::size_t column = 0; column < dimension_; column++) {
            dot_product +=
                static_cast<double>(query_embedding[column]) *
                static_cast<double>(embeddings_[offset + column]);
        }

        // cosine = dot(query, document) / norm(query)
        double cosine_similarity = dot_product / query_norm;

        cosine_similarity = std::clamp(
            cosine_similarity,
            -1.0, 1.0
        );

        DenseVectorSearchHit hit;
        hit.row = row;
        hit.chunk_id = chunk_ids_[row];
        hit.score = static_cast<float>(cosine_similarity);

        results.push_back(hit);
    }

    std::sort(
        results.begin(),
        results.end(),
        [](
            const DenseVectorSearchHit& left,
            const DenseVectorSearchHit& right
        ) {
            if (left.score != right.score) {
                return left.score > right.score;
            }

            return left.chunk_id < right.chunk_id;
        }
    );

    const std::size_t result_limit = static_cast<std::size_t>(top_k);

    if (results.size() > result_limit) {
        results.resize(result_limit);
    }

    return results;
}

bool DenseVectorIndex::isLoaded() const noexcept {
    return loaded_;
}

std::size_t DenseVectorIndex::rowCount() const noexcept {
    return rows_;
}

std::size_t DenseVectorIndex::dimension() const noexcept {
    return dimension_;
}

int DenseVectorIndex::maxLength() const noexcept {
    return max_length_;
}

const std::vector<int>& DenseVectorIndex::chunkIds() const noexcept {
    return chunk_ids_;
}

const std::string& DenseVectorIndex::modelId() const noexcept {
    return model_id_;
}

const std::string& DenseVectorIndex::pooling() const noexcept {
    return pooling_;
}

const std::string& DenseVectorIndex::queryInstruction() const noexcept {
    return query_instruction_;
}

const std::string& DenseVectorIndex::lastError() const noexcept {
    return last_error_;
}

const std::string& DenseVectorIndex::chunksFingerprint() const noexcept {
    return chunks_fingerprint_;
}
