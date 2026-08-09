#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "alsa_audio_player.h"
#include "alsa_pcm_stream_source.h"
#include "board_app_config.h"
#include "continuous_voice_session.h"
#include "llm_backend_factory.h"
#include "policy_routing_answer_backend.h"
#include "retriever_runtime.h"
#include "sherpa_onnx_asr_backend.h"
#include "sherpa_onnx_tts_backend.h"
#include "sherpa_onnx_vad_audio_recorder.h"
#include "voice_assistant.h"
#include "voice_performance_report.h"

#include "agent/agent_answer_backend.h"
#include "agent/agent_executor.h"
#include "agent/agent_planner.h"
#include "agent/agent_routing_answer_backend.h"
#include "agent/llm_agent_planner.h"
#include "agent/rule_agent_planner.h"
#include "agent/tool_registry.h"
#include "agent/vehicle_device.h"
#include "agent/vehicle_tools.h"

namespace {

namespace fs = std::filesystem;
using namespace edge::agent;

void printEvent(const VoiceSessionEvent& event) {
    switch (event.type) {
        case VoiceSessionEventType::WaitingForSpeech:
            std::cout << "[VOICE] 等待用户说话...\n";
            break;

        case VoiceSessionEventType::RecognizedText:
            std::cout
                << "[ASR] "
                << event.text
                << '\n';
            break;

        case VoiceSessionEventType::AnswerCompleted:
            std::cout
                << "[ANSWER] "
                << event.text
                << '\n';

            std::cout
                << "[POLICY] mode="
                << event.assistant_result.response_mode
                << " category="
                << event.assistant_result.query_category
                << " confidence="
                << event.assistant_result.classification_confidence
                << " retrieved="
                << event.assistant_result.retrieval_result_count
                << " reason="
                << event.assistant_result.response_reason
                << '\n';

            std::cout
                << "[PERF_JSON] "
                << VoicePerformanceReport::encode(event)
                << '\n';

            break;

        case VoiceSessionEventType::BargeInDetected:
            std::cout << "[BARGE-IN] 检测到用户打断\n";
            break;

        case VoiceSessionEventType::Error:
            std::cerr
                << "[ERROR] "
                << event.error
                << '\n';
            break;
    }
}

}   // namespace

int main(int argc, char* argv[]) {
    // 调试日志使用，正常情况下可注释
    std::cout << std::unitbuf;
    std::cout << std::unitbuf;
    const std::string config_path = argc >= 2 ? argv[1] : "config/board_rk3576.conf";

    try {
        const BoardAppConfig config = BoardAppConfig::load(config_path);

        RetrieverRuntimeConfig retriever_config;
        retriever_config.backend = config.retrieval_backend;
        retriever_config.knowledge_path = config.knowledge_path;
        retriever_config.bge_model_path = config.bge_model_path;
        retriever_config.bge_tokenizer_path = config.bge_tokenizer_path;
        retriever_config.dense_index_metadata_path = config.dense_index_metadata_path;
        retriever_config.dense_embeddings_path = config.dense_embeddings_path;
        retriever_config.dense_minimum_similarity = config.dense_minimum_similarity;
        retriever_config.hybrid_rrf_k = config.hybrid_rrf_k;
        retriever_config.hybrid_dense_weight = config.hybrid_dense_weight;
        retriever_config.hybrid_sparse_weight = config.hybrid_sparse_weight;
        retriever_config.hybrid_candidate_top_k = config.hybrid_candidate_top_k;
        retriever_config.relevance_filter_enabled = config.relevance_filter_enabled;
        retriever_config.relevance_minimum_sparse_score = config.relevance_minimum_sparse_score;
        retriever_config.relevance_minimum_dense_similarity = config.relevance_minimum_dense_similarity;
        retriever_config.relevance_candidate_top_k = config.relevance_candidate_top_k;

        RetrieverRuntime retriever_runtime(std::move(retriever_config));

        if (!retriever_runtime.load()) {
            throw std::runtime_error(
                retriever_runtime.lastError()
            );
        }

        LlmBackendOptions llm_options;
        llm_options.backend = config.llm_backend;
        llm_options.model_path = config.llm_model_path;
        llm_options.max_new_tokens = config.llm_max_new_tokens;
        llm_options.max_context_len = config.llm_max_context_len;

        std::unique_ptr<LlmBackend> llm_backend = createLlmBackend(llm_options);

        MockVehicleDevice vehicle_device;
        vehicle_device.setEnvironment(
            config.agent_mock_temperature_c,
            config.agent_mock_humidity_percent
        );

        ToolRegistry agent_tools;
        agent_tools.registerTool(
            std::make_unique<GetCabinEnvironmentTool>(vehicle_device)
        );
        agent_tools.registerTool(
            std::make_unique<GetAirConditionerStateTool>(vehicle_device)
        );
        agent_tools.registerTool(
            std::make_unique<SetAirConditionerTool>(vehicle_device)
        );

        std::string resolved_agent_planner = config.agent_planner;

        if (resolved_agent_planner == "auto") {
            resolved_agent_planner = config.llm_backend == "mock" ? "rule" : "llm";
        }

        std::unique_ptr<AgentPlanner> agent_planner;
        if (resolved_agent_planner == "rule") {
            agent_planner = std::make_unique<RuleAgentPlanner>();
        } else {
            agent_planner = std::make_unique<LlmAgentPlanner>(
                *llm_backend,
                agent_tools
            );
        }

        AgentExecutorConfig agent_executor_config;
        agent_executor_config.maximum_steps = config.agent_max_steps;
        agent_executor_config.confirmation_timeout = std::chrono::milliseconds(
            config.agent_confirmation_timeout_ms
        );

        AgentExecutor agent_executor(
            *agent_planner,
            agent_tools,
            agent_executor_config
        );

        AgentAnswerBackend agent_answer_backend(agent_executor);

        PolicyRoutingAnswerBackendConfig answer_config;
        answer_config.top_k = config.top_k;
        answer_config.policy.direct_rag_minimum_sparse_score =
            config.response_direct_minimum_sparse_score;
        answer_config.policy.direct_rag_minimum_dense_similarity =
            config.response_direct_minimum_dense_similarity;

        PolicyRoutingAnswerBackend policy_answer_backend(
            retriever_runtime.retriever(),
            *llm_backend,
            answer_config
        );

        AgentRoutingAnswerBackend answer_backend(
            agent_answer_backend,
            policy_answer_backend,
            policy_answer_backend
        );

        // 语音转文字
        SherpaOnnxAsrConfig asr_config;
        asr_config.model_path = config.asr_model_path;
        asr_config.tokens_path = config.asr_tokens_path;
        asr_config.language = "zh";
        asr_config.use_itn = true;
        asr_config.num_threads = config.asr_num_threads;
        asr_config.provider = "cpu";

        SherpaOnnxAsrBackend asr_backend(asr_config);

        const fs::path tts_dir = config.tts_model_dir;

        // 文字转语音
        SherpaOnnxTtsConfig tts_config;
        tts_config.model_path = (tts_dir/"model.int8.onnx").string();
        tts_config.lexicon_path = (tts_dir/"lexicon.txt").string();
        tts_config.tokens_path = (tts_dir/"tokens.txt").string();
        tts_config.rule_fsts = {
            (tts_dir/"date.fst").string(),
            (tts_dir/"number.fst").string()
        };
        tts_config.num_threads = config.tts_num_threads;
        tts_config.speaker_id = 0;
        tts_config.speed = 1.0F;
        tts_config.provider = "cpu";

        SherpaOnnxTtsBackend tts_backend(tts_config);

        // 播放器
        AlsaAudioPlayerConfig player_config;
        player_config.device = config.playback_device;
        player_config.period_frames = 1024;

        AlsaAudioPlayer audio_player(player_config);

        // 语音采集
        AlsaPcmStreamSourceConfig pcm_config;
        pcm_config.device = config.capture_device;
        pcm_config.sample_rate = 16000;
        pcm_config.channels = 1;
        pcm_config.chunk_frames = 512;

        AlsaPcmStreamSource pcm_source(pcm_config);

        // 正常语音检测
        SherpaOnnxVadConfig normal_vad;
        normal_vad.model_path = config.vad_model_path;
        normal_vad.num_threads = config.vad_num_threads;
        normal_vad.threshold = config.normal_vad_threshold;
        normal_vad.min_silence_duration = config.normal_silence_seconds;
        normal_vad.min_speech_duration = 0.25F;
        normal_vad.max_speech_duration = 15.0F;

        // 正常模式长时间等待用户开口
        normal_vad.max_wait_seconds = 3600.0F;

        // 打断语音检测
        SherpaOnnxVadConfig barge_vad = normal_vad;

        barge_vad.threshold = config.barge_vad_threshold;
        barge_vad.min_silence_duration = config.barge_silence_seconds;
        barge_vad.min_speech_duration = 0.15F;
        barge_vad.max_wait_seconds = 60.0F;

        SherpaOnnxVadAudioRecorder normal_recorder(pcm_source, normal_vad);
        SherpaOnnxVadAudioRecorder barge_recorder(pcm_source, barge_vad);

        VoiceAssistant assistant(
            answer_backend,
            tts_backend,
            audio_player,
            answer_backend
        );

        ContinuousVoiceSessionConfig session_config;
        session_config.max_turns = config.max_turns;

        ContinuousVoiceSession session(
            normal_recorder,
            barge_recorder,
            asr_backend,
            assistant,
            session_config
        );

        std::cout
            << "EdgeVoiceRAG board assistant\n"
            << "Retriever: "
            << retriever_runtime.backendName()
            << "\nRelevance filter: "
            << (
                retriever_runtime.relevanceFilterEnabled()
                    ? "enabled"
                    : "disabled"
            )
            << "\nLLM: "
            << llm_backend->name()
            << "\nResponse policy: "
            << answer_backend.name()
            << "\nASR: "
            << asr_backend.name()
            << "\nTTS: "
            << tts_backend.name()
            << "\nCapture: "
            << pcm_source.name()
            << "\nPlayback: "
            << audio_player.name()
            << "\nAgent planner: "
            << agent_planner->name()
            << "\nAgent maximum steps: "
            << config.agent_max_steps
            << "\nAgent confirmation timeout: "
            << config.agent_confirmation_timeout_ms
            << " ms"
            << "\nAgent tools: "
            << agent_tools.definitions().dump()
            << "\n";

        const ContinuousVoiceSessionResult result = session.run(printEvent);

        if (!result.ok) {
            std::cerr
                << "Voice session failed: "
                << result.error
                << '\n';

            return 1;
        }

        std::cout
            << "Completed turns: "
            << result.completed_turns
            << "\nInterruptions: "
            << result.interruption_count
            << '\n';

        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "Board application failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
