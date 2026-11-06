/*
 * 用户问题与每个chunk的retrieval_text计算余弦相似度
 * BGE是针对Query-Document检索进行对比学习的双塔Embedding模型，训练目标就是让能够回答某个问题的文档向量靠近该问题向量
*/

#include "bge_embedder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <tokenizers_cpp.h>

namespace {
std::string loadBinaryFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);

    if (!input.is_open()) {
        throw std::runtime_error(
            "failed to open file: " + path
        );
    }

    const std::streampos end_position = input.tellg();

    if (end_position <= 0) {
        throw std::runtime_error(
            "file is empty: " + path
        );
    }

    std::string data(static_cast<std::size_t>(end_position), '\0');

    input.seekg(0, std::ios::beg);

    input.read(
        data.data(),
        static_cast<std::streamsize>(data.size())
    );

    if (
        !input ||
        static_cast<std::size_t>(input.gcount() != data.size())
    ) {
        throw std::runtime_error(
            "failed to read complete file: " +
            path
        );
    }

    return data;
}

BgeEmbeddingResult failureResult(const std::string& error) {
    BgeEmbeddingResult result;
    result.ok = false;
    result.error = error;

    return result;
}

}   // namespace

struct BgeEmbedder::Impl {

    Ort::Env environment;
    Ort::SessionOptions session_options;

    std::unique_ptr<Ort::Session> session;

    std::unique_ptr<tokenizers::Tokenizer> tokenizer;

    int32_t cls_token_id = -1;
    int32_t sep_token_id = -1;

    /*
     * tokenizers-cpp的Encode接口不是const；
     * 同一个BgeEmbedder可能被多个线程查询，
     * 因此先用互斥锁保证安全。
     */
    mutable std::mutex inference_mutex;
};

BgeEmbedder::BgeEmbedder(BgeEmbedderConfig config)
    : config_(config)
    , impl_(std::make_unique<Impl>()) {}

BgeEmbedder::~BgeEmbedder() = default;

bool BgeEmbedder::load() {
    loaded_ = false;
    last_error_.clear();

    // 丢弃可能存在的半初始化状态。
    impl_ = std::make_unique<Impl>();

    if (config_.model_path.empty()) {
        last_error_ = "BGE model path must not be empty";

        return false;
    }

    if (config_.tokenizer_path.empty()) {
        last_error_ = "BGE tokenizer path must not be empty";

        return false;
    }

    if (config_.max_length < 2) {
        last_error_ =  "BGE max_length must be at least two";

        return false;
    }

    if (config_.expected_dimension <= 0) {
        last_error_ = "BGE expected dimension must be positive";

        return false;
    }

    if (
        config_.intra_op_threads <= 0 ||
        config_.inter_op_threads <= 0
    ) {
        last_error_ = "BGE thread counts must be positive";

        return false;
    }

    try {
        const std::string tokenizer_blob = 
            loadBinaryFile(config_.tokenizer_path);

        impl_->tokenizer = tokenizers::Tokenizer::FromBlobJSON(tokenizer_blob);

        if (!impl_->tokenizer) {
            throw std::runtime_error(
                "failed to create tokenizer "
                "from tokenizer.json"
            );
        }

        impl_->cls_token_id = impl_->tokenizer->TokenToId("[CLS]");
        impl_->sep_token_id = impl_->tokenizer->TokenToId("[SEP]");

        if (
            impl_->cls_token_id < 0 ||
            impl_->sep_token_id < 0
        ) {
            throw std::runtime_error(
                "tokenizer does not contain "
                "[CLS] or [SEP]"
            );
        }

        impl_->session_options.SetIntraOpNumThreads(config_.intra_op_threads);
        impl_->session_options.SetInterOpNumThreads(config_.inter_op_threads);
        impl_->session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl_->session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        impl_->session = std::make_unique<Ort::Session>(
            impl_->environment,
            config_.model_path.c_str(),
            impl_->session_options
        );

        Ort::AllocatorWithDefaultOptions allocator;

        std::unordered_set<std::string> input_names;

        const std::size_t input_count = impl_->session->GetInputCount();

        for (std::size_t index = 0; index < input_count; index++) {
            auto name = impl_->session->GetInputNameAllocated(index, allocator);

            input_names.insert(name.get());
        }

        const std::unordered_set<std::string> expected_inputs{
            "input_ids",
            "attention_mask",
            "token_type_ids"
        };

        if (input_names != expected_inputs) {
            throw std::runtime_error(
                "unexpected BGE ONNX input names"
            );
        }

        if (impl_->session->GetOutputCount() != 1) {
            throw std::runtime_error(
                "BGE ONNX model must have "
                "exactly one output"
            );
        }

        auto output_name = impl_->session->GetOutputNameAllocated(0, allocator);

        if (std::string(output_name.get()) != "embeddings") {
            throw std::runtime_error(
                "BGE ONNX output must be "
                "named embeddings"
            );
        }

        const Ort::TypeInfo output_type = impl_->session->GetOutputTypeInfo(0);
        const auto output_tensor_info = output_type.GetTensorTypeAndShapeInfo();

        if (
            output_tensor_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
        ) {
            throw std::runtime_error(
                "BGE ONNX output must be float32"
            );
        }

        const std::vector<std::int64_t> output_shape = output_tensor_info.GetShape();

        if (
            output_shape.size() != 2 ||
            (output_shape[1] > 0 && output_shape[1] != config_.expected_dimension) 
        ) {
            throw std::runtime_error(
                "unexpected BGE ONNX output shape"
            );
        }

        // 真实测试
        const std::vector<int32_t> test_ids = impl_->tokenizer->Encode("测试");

        if (test_ids.empty()) {
            throw std::runtime_error(
                "tokenizer returned no token IDs"
            );
        }

        loaded_ = true;
        last_error_.clear();

        return true;
    } catch (const Ort::Exception& error) {
        last_error_ = "ONNX Runtime error: " + std::string(error.what());

        return false;
    } catch (const std::exception& error) {
        last_error_ = error.what();
        return false;
    }
}

BgeEmbeddingResult BgeEmbedder::encodeQuery(const std::string& query) const {
    if (!loaded_ || !impl_->session) {
        return failureResult("BGE embedder is not loaded");
    }

    if (query.empty()) {
        return failureResult("BGE query must not be empty");
    }

    std::lock_guard<std::mutex> lock(impl_->inference_mutex);

    try {
        const std::string input_text = config_.query_instruction + query;

        std::vector<int32_t> token_ids = impl_->tokenizer->Encode(input_text);

        if (token_ids.empty()) {
            return failureResult("tokenizer returned no token IDs");
        }

        const auto max_length = static_cast<std::size_t>(config_.max_length);

        if (max_length < 2) {
            return failureResult(
                "BGE max_length must be at least 2"
            );
        }

        if (token_ids.front() != impl_->cls_token_id) {
            token_ids.insert(token_ids.begin(), impl_->cls_token_id);
        }

        if (token_ids.size() > max_length) {
            token_ids.resize(max_length);
        }

        if (token_ids.back() != impl_->sep_token_id) {
            if (token_ids.size() == max_length) {
                token_ids.back() = impl_->sep_token_id;
            } else {
                token_ids.push_back(impl_->sep_token_id);
            }
        }

        std::vector<std::int64_t> input_ids;
        input_ids.reserve(token_ids.size());

        for (const int32_t token_id : token_ids) {
            input_ids.push_back(static_cast<std::int64_t>(token_id));
        }

        std::vector<std::int64_t> attention_mask(input_ids.size(), 1);
        std::vector<std::int64_t> token_type_ids(input_ids.size(), 0);

        // 构造tensor形状，第一个参数表示一次处理多少个句子，第二个参数表示这句话有多少个token
        const std::array<std::int64_t, 2> input_shape{
            1,
            static_cast<std::int64_t>(input_ids.size())
        };

        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault
        );

        std::vector<Ort::Value> input_tensors;
        input_tensors.reserve(3);

        input_tensors.push_back(
            Ort::Value::CreateTensor<std::int64_t>(
                memory_info,
                input_ids.data(),
                input_ids.size(),
                input_shape.data(),
                input_shape.size()
            )
        );

        input_tensors.push_back(
            Ort::Value::CreateTensor<std::int64_t>(
                memory_info,
                attention_mask.data(),
                attention_mask.size(),
                input_shape.data(),
                input_shape.size()
            )
        );

        input_tensors.push_back(
            Ort::Value::CreateTensor<std::int64_t>(
                memory_info,
                token_type_ids.data(),
                token_type_ids.size(),
                input_shape.data(),
                input_shape.size()
            )
        );

        const std::array<const char*, 3> input_names{
            "input_ids",
            "attention_mask",
            "token_type_ids"
        };

        const std::array<const char*, 1> output_names{
            "embeddings"
        };

        std::vector<Ort::Value> outputs = 
            impl_->session->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                input_tensors.data(),
                input_tensors.size(),
                output_names.data(),
                output_names.size()
            );

        if (
            outputs.size() != 1 || !outputs[0].IsTensor()
        ) {
            return failureResult(
                "BGE ONNX returned invalid output"
            );
        }

        const Ort::TensorTypeAndShapeInfo output_info = 
            outputs[0].GetTensorTypeAndShapeInfo();

        const std::vector<std::int64_t> output_shape = output_info.GetShape();

        if (
            output_shape.size() != 2 ||
            output_shape[0] != 1 ||
            output_shape[1] != config_.expected_dimension
        ) {
            return failureResult(
                "BGE output shape does not match "
                "expected dimension"
            );
        }

        const std::size_t element_count = output_info.GetElementCount();

        if (
            element_count != static_cast<std::size_t>(config_.expected_dimension)
        ) {
            return failureResult(
                "BGE output element count mismatch"
            );
        }

        const float* output_data = outputs[0].GetTensorData<float>();

        std::vector<float> embedding(
            output_data,
            output_data + element_count
        );

        double squared_norm = 0.0;

        for (const float value : embedding) {
            if (!std::isfinite(value)) {
                return failureResult(
                    "BGE output contains NaN or Inf"
                );
            }

            squared_norm += static_cast<double>(value) * static_cast<double>(value);
        }

        if (squared_norm <= 0.0) {
            return failureResult(
                "BGE output is a zero vector"
            );
        }

        /*
         * ONNX模型内部已经归一化；
         * 这里再次归一化可消除微小浮点误差。
         */
        const float norm = static_cast<float>(std::sqrt(squared_norm));

        for (float& value : embedding) {
            value /= norm;
        }

        BgeEmbeddingResult result;
        result.ok = true;
        result.embedding = std::move(embedding);

        return result;
    } catch(const Ort::Exception& error) {
        return failureResult(
            "ONNX Runtime error: " +
            std::string(error.what())
        );
    } catch (const std::exception& error) {
        return failureResult(
            error.what()
        );
    }
}

bool BgeEmbedder::isLoaded() const noexcept {
    return loaded_;
}

int BgeEmbedder::dimension() const noexcept {
    return config_.expected_dimension;
}

const std::string&
BgeEmbedder::lastError() const noexcept {
    return last_error_;
}
