#include "rkllm_backend.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include <rkllm.h>

struct RkllmBackend::Impl {
    LLMHandle handle = nullptr;     //推理实例的不透明句柄
};

namespace {

struct GenerationContext {
    std::string answer;
    std::string error;
    bool finished = false;
};

// 生成token时，runtime回调用该函数
// result: 本次回调产生的结果；通常是一小段增量文本，模型不会一定等完整回答生成后才一次性返回
// userdata: 调用run时传入的用户上下文指针， run中的最后一个参数是userdata，该模块为context
int resultCallback(RKLLMResult* result, void* userdata, LLMCallState state) {
    auto* context = static_cast<GenerationContext*>(userdata);

    if (context == nullptr) {
        return -1;
    }

    // RKLLM_RUN_NORMAL：正常生成文本
    if (state == RKLLM_RUN_NORMAL) {
        if (result != nullptr && result->text != nullptr) {
            context->answer += result->text;
        }

        return 0;
    }

    // RKLLM_RUN_FINISH: 本次生成结束
    if (state == RKLLM_RUN_FINISH) {
        context->finished = true;
        return 0;
    }
    // RKLLM_RUN_ERROR：生成发生错误
    if (state == RKLLM_RUN_ERROR) {
        context->error = "RKLLM generation failed";
        context->finished = true;
        return -1;
    }

    return 0;
}

}   // namespace

RkllmBackend::RkllmBackend(const RkllmBackendConfig& config)
    : impl_(new Impl) {
    if (config.model_path.empty()) {
        delete impl_;
        impl_ = nullptr;
        throw std::invalid_argument("RKLLM model path must not be empty");
    }

    RKLLMParam param = rkllm_createDefaultParam();

    param.model_path = config.model_path.c_str();
    param.max_new_tokens = config.max_new_tokens;
    param.max_context_len = config.max_context_len;
    param.top_k = config.top_k;                         // 只保留概率最高的k个候选token
    param.top_p = config.top_p;                         // 核采样，从概率最高的token开始累加频率，直到累计频率超过top_p，后面的token删除
    param.temperature = config.temperature;             // 控制概率分布的随机程度
    param.repeat_penalty = config.repeat_penalty;       // 惩罚已经生成过的token，减少重复和循环输出
    param.frequency_penalty = 0.0F;                     // 按照token已经出现的次数进行惩罚
    param.presence_penalty = 0.0F;                      // 只要出现过就施加惩罚
    param.skip_special_token = true;                    // 从最终输出中跳过模型的特殊token
    param.extend_param.base_domain_id = 0;              // RKLLM/RKNPU 内存映射相关的基础 IOMMU domain 配置
    param.extend_param.embed_flash = 1;                 // 控制是否使用Flash存储或访问Embedding权重

    // 回调配置结构体，结构体根据模型生成内容或状态变化不同选择调用不同的函数通知应用程序
    RKLLMCallback callback{};
    callback.result_callback = resultCallback;

    // 保存配置、注册回调、创建完整的RKLLM推理实例
    const int result = rkllm_init(&impl_->handle, &param, &callback);

    if (result != 0 || impl_->handle == nullptr) {
        delete impl_;
        impl_ = nullptr;
        throw std::runtime_error(
            "failed to initialize RKLLM model: " + config.model_path
        );
    }
}

RkllmBackend::~RkllmBackend() {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->handle != nullptr) {
        rkllm_destroy(impl_->handle);
        impl_->handle = nullptr;
    }

    delete impl_;
    impl_ = nullptr;
}

std::string RkllmBackend::name() const {
    return "rkllm";
}

LlmGenerationResult RkllmBackend::generate(const std::string& prompt) {
    if (prompt.empty()) {
        return LlmGenerationResult::failure("prompt must not be empty");
    }

    if (impl_ == nullptr || impl_->handle == nullptr) {
        return LlmGenerationResult::failure(
            "RKLLM backend is not initialized"
        );
    }

    // 模型输入
    RKLLMInput input{};
    input.input_type = RKLLM_INPUT_PROMPT;  // 表示对字符串进行模板处理、分词和token编码
    input.role = const_cast<char*>("user"); // 输入信息角色为用户
    input.prompt_input = const_cast<char*>(prompt.c_str());

    // 本次推理的控制参数
    RKLLMInferParam infer_param{};
    infer_param.mode = RKLLM_INFER_GENERATE;    // 模型需要根据prompt自回归生成回答
    infer_param.keep_history = 0;               // 本次推理结束后不保留对话历史

    GenerationContext context;

    const int result = rkllm_run(
        impl_->handle,
        &input,
        &infer_param,
        &context
    );

    if (result != 0) {
        return LlmGenerationResult::failure(
            "rkllm_run returned error code: " + std::to_string(result)
        );
    }

    if (!context.error.empty()) {
        return LlmGenerationResult::failure(context.error);
    }

    if (!context.finished) {
        return LlmGenerationResult::failure(
            "RKLLM generation ended without finish callback"
        );
    }

    return LlmGenerationResult::success(context.answer);
}