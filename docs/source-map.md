# 源码文件索引

本索引覆盖本次目录与构建整理提交中的源码；此前独立的Planner评测工作单独列在文末，并已分步提交。公共接口的逐项说明见 [api-reference.md](api-reference.md)；训练数据与本地模型不在这里重复展开。

## C++ 实现

### agent

| 文件 | 职责 |
| --- | --- |
| [agent_answer_backend.cpp](../src/agent/agent_answer_backend.cpp) | 把 AgentResponse 包装为流式回答接口并记录执行结果；默认固定 session_id 保存跨轮确认。 |
| [agent_executor.cpp](../src/agent/agent_executor.cpp) | 控制多步循环与每会话待确认动作，默认4步/30秒；注入 now 便于超时测试；取消不回滚已执行工具。 |
| [agent_intent_classifier.cpp](../src/agent/agent_intent_classifier.cpp) | 规则区分环境查询、设备状态、控制和条件工作流；None 回退普通问答，含 confidence/reason。 |
| [agent_routing_answer_backend.cpp](../src/agent/agent_routing_answer_backend.cpp) | 安全分类、pending 和意图分发；追踪活动后端，让取消传到正确目标。 |
| [llm_agent_planner.cpp](../src/agent/llm_agent_planner.cpp) | 以工具定义与历史构建 prompt，解析 JSON、校验工具；maximum_response_bytes 默认 8192；parseResponse/buildPrompt 也供测试。 |
| [rule_agent_planner.cpp](../src/agent/rule_agent_planner.cpp) | 确定性规则和 Observation 回答，用于无模型基线；不等同于开放式多条件理解。 |
| [tool_registry.cpp](../src/agent/tool_registry.cpp) | 注册并拥有工具，find 返回非拥有指针；execute 通过白名单分发并返回 AgentToolResult；definitions 提供模型可见 schema。 |
| [vehicle_tools.cpp](../src/agent/vehicle_tools.cpp) | 五类工具共用 name/description/parametersSchema/execute；后两类要求确认；参数、结果和错误见 protocols.md。 |

### apps

| 文件 | 职责 |
| --- | --- |
| [agent_demo.cpp](../src/apps/agent_demo.cpp) | RulePlanner文本Agent，默认Mock，支持--device iio及IIO根目录/name参数。 |
| [audio_output_check.cpp](../src/apps/audio_output_check.cpp) | 独立ALSA输出探针：tone/可选TTS、同handle重复播放、PCM16 WAV无覆盖导出与JSONL诊断；不启动录音/VAD/ASR/Agent。 |
| [board_voice_main.cpp](../src/apps/board_voice_main.cpp) | 板端连续语音主入口；本地装配检索、推理、设备 Agent 和音频后端。 |
| [llm_server.cpp](../src/apps/llm_server.cpp) | C++ LLM 普通/流式服务，启动独立取消线程。 |
| [main.cpp](../src/apps/main.cpp) | 文本 CLI，装配 AppConfig、BM25 和旧多级响应系统。 |
| [rag_client.cpp](../src/apps/rag_client.cpp) | C++ RAG 文本 REQ CLI。 |
| [rag_server.cpp](../src/apps/rag_server.cpp) | 最早的 C++ BM25 文本 REP 服务，默认5555。 |
| [sherpa_asr_demo.cpp](../src/apps/sherpa_asr_demo.cpp) | 可选 sherpa ASR 示例入口。 |
| [zmq_rep_server.cpp](../src/apps/zmq_rep_server.cpp) | 最小 ZeroMQ REP 联通示例。 |
| [zmq_req_client.cpp](../src/apps/zmq_req_client.cpp) | 最小 ZeroMQ REQ 联通示例。 |

### audio

| 文件 | 职责 |
| --- | --- |
| [alsa_audio_player.cpp](../src/audio/alsa_audio_player.cpp) | ALSA 播放适配，device 与 period_frames 可配置；RAII + PImpl，禁复制；需 EDGE_ENABLE_ALSA。 |
| [alsa_audio_recorder.cpp](../src/audio/alsa_audio_recorder.cpp) | 固定时长 ALSA 录音，record_duration_ms/采样率/声道/period 可配置；需 EDGE_ENABLE_ALSA。 |
| [alsa_pcm_stream_source.cpp](../src/audio/alsa_pcm_stream_source.cpp) | 连续 PCM chunk 源；默认 16000Hz、单声道、512 帧与 100000us 延迟；需 EDGE_ENABLE_ALSA。 |
| [wav_audio_reader.cpp](../src/audio/wav_audio_reader.cpp) | 解析 RIFF/WAVE 和 PCM 数据，跳过支持的可选块；不支持或损坏输入抛异常；不是任意音频格式解码器。 |
| [wav_audio_recorder.cpp](../src/audio/wav_audio_recorder.cpp) | 把 WAV 当作录音输入供离线流水线使用；停止状态通过 atomic 保存。 |

### common

| 文件 | 职责 |
| --- | --- |
| [file_fingerprint.cpp](../src/common/file_fingerprint.cpp) | 读取原始字节，返回固定 16 位小写十六进制指纹或 error；用于一致性检查，不是密码学完整性验证。 |
| [logger.cpp](../src/common/logger.cpp) | 静态日志输出，区分 Info/Warning/Error/User/Route/System/Perf。 |
| [perf_timer.cpp](../src/common/perf_timer.cpp) | steady_clock 计时器，单位毫秒；用于阶段耗时，不是 CPU 时间计数器。 |

### config

| 文件 | 职责 |
| --- | --- |
| [app_config.cpp](../src/config/app_config.cpp) | 文本 CLI 配置。load 返回 bool；读取手册路径、远程端口和超时，不承担语音模型配置。 |
| [board_app_config.cpp](../src/config/board_app_config.cpp) | 板端语音/检索/Agent配置；无效文件或值抛异常；默认值与实际 conf 覆盖值应区分。 |
| [command_line_options.cpp](../src/config/command_line_options.cpp) | 解析文本 CLI 的 --config、--once、--help；错误保存在对象内供入口决定退出。 |

### devices

| 文件 | 职责 |
| --- | --- |
| [linux_vehicle_device.cpp](../src/devices/linux_vehicle_device.cpp) | Linux IIO读取与内存空调状态；对应 agent/linux_vehicle_device.h。 |
| [vehicle_device.cpp](../src/devices/vehicle_device.cpp) | MockVehicleDevice实现，注入环境与控制失败。 |

### inference

| 文件 | 职责 |
| --- | --- |
| [llm_backend_factory.cpp](../src/inference/llm_backend_factory.cpp) | 选择 mock/rkllm；未开启 RKLLM 或不支持配置时拒绝；构造时完成后端初始化。 |
| [mock_llm_backend.cpp](../src/inference/mock_llm_backend.cpp) | 确定性回答与流式联调后端；普通 Mock 输出不满足 Planner 工具 JSON，不能替代 Scripted LLM 或 Rule Planner。 |
| [rkllm_backend.cpp](../src/inference/rkllm_backend.cpp) | 通过 rkllm_init/run/abort/destroy 管理 SDK handle，回调传递 delta；禁复制，需 EDGE_ENABLE_RKLLM 及匹配模型。 |

### observability

| 文件 | 职责 |
| --- | --- |
| [voice_performance_report.cpp](../src/observability/voice_performance_report.cpp) | 仅接受有效 AnswerCompleted 和一致 request_id；返回单行 JSON 字符串，不含外部 [PERF_JSON] 前缀；非法事件抛异常。 |
| [voice_performance_statistics.cpp](../src/observability/voice_performance_statistics.cpp) | 普通行返回 false 且 error 空，坏报告返回 false 且 error 非空；汇总平均/P50/P95/最大值和有效/失败样本计数。 |

### response

| 文件 | 职责 |
| --- | --- |
| [cancellable_answer_backend.cpp](../src/response/cancellable_answer_backend.cpp) | 请求取消，返回 ok/cancelled/request_id/active_request_id/error；成功收到响应和实际取消是不同状态。 |
| [edge_response_backend.cpp](../src/response/edge_response_backend.cpp) | 旧文本系统的适配器；local 走本地检索，远程走 ZeroMQ；LLM 请求经协议编解码。 |
| [local_rag_llm_backend.cpp](../src/response/local_rag_llm_backend.cpp) | 固定 RAG+LLM 流水线，拼手册 prompt，转发 chunk 并计时；取消仅针对匹配的活动 request_id。 |
| [multi_level_response_system.cpp](../src/response/multi_level_response_system.cpp) | 旧文本路由与进程内缓存；QueryProcessResult 含 ok/answer/error/category/mode/from_cache。 |
| [policy_routing_answer_backend.cpp](../src/response/policy_routing_answer_backend.cpp) | 按 ResponsePolicy 执行固定回复、直答、RAG+LLM、纯 LLM 或澄清，结果携带策略原因。 |
| [query_classifier.cpp](../src/response/query_classifier.cpp) | 提取关键词、紧急/事实/复杂/创作特征并给类别、confidence、reasoning 和 requires_immediate_response。 |
| [query_router.cpp](../src/response/query_router.cpp) | 早期 VehicleManual/chat/Unknown 分类接口，主要保留给原有调用和测试；不要与 QueryClassifier 混同。 |
| [response_policy.cpp](../src/response/response_policy.cpp) | 纯策略，返回 decision 包含类别、模式、检索数量、置信判断及 reason；不执行模型。 |
| [streaming_answer_backend.cpp](../src/response/streaming_answer_backend.cpp) | 统一本地/远程流式回答；回调接收 RagStreamEvent，返回累计回答、ok/error 和检索/LLM时序，策略元数据可选。 |

### retrieval

| 文件 | 职责 |
| --- | --- |
| [bge_embedder.cpp](../src/retrieval/bge_embedder.cpp) | ONNX Runtime CPU + tokenizers-cpp；普通编码与显式Profiled分段路径，返回向量、状态和可选timing；需BGE开关。 |
| [bm25_retriever.cpp](../src/retrieval/bm25_retriever.cpp) | 建立内存倒排统计并按 BM25 排序；配置 k1/b/minimum_matched_terms；应先成功加载。 |
| [chinese_text_tokenizer.cpp](../src/retrieval/chinese_text_tokenizer.cpp) | 中文文本分词，供 BM25 构建/查询词项；精确签名见头文件，不使用 Python jieba 实例。 |
| [dense_retriever.cpp](../src/retrieval/dense_retriever.cpp) | 组合BGE、DenseVectorIndex与手册映射；校验chunks兼容性，支持显式编码/索引/映射分段；需BGE开关。 |
| [dense_vector_index.cpp](../src/retrieval/dense_vector_index.cpp) | 载入连续 float32 向量及元数据，查询归一化向量并返回 row/chunk_id/score；负责格式、大小等校验。 |
| [hybrid_retriever.cpp](../src/retrieval/hybrid_retriever.cpp) | 非拥有引用组合双路检索；加权 RRF 合并去重；candidate_top_k 控制候选数。 |
| [knowledge_base_loader.cpp](../src/retrieval/knowledge_base_loader.cpp) | 解析 chunks，返回 ok/documents/error；统一手册与别名构造规则。 |
| [relevance_filtering_retriever.cpp](../src/retrieval/relevance_filtering_retriever.cpp) | 装饰器过滤原始 sparse/dense 分数不达标结果，再截取 top_k；不能用 RRF 分数替代原始分数。 |
| [retriever_runtime.cpp](../src/retrieval/retriever_runtime.cpp) | 拥有检索器和过滤器，load 成功后才可访问 retriever 引用；未加载时访问抛 logic_error；关闭 BGE 时拒绝 dense/hybrid。 |

### speech

| 文件 | 职责 |
| --- | --- |
| [sherpa_onnx_asr_backend.cpp](../src/speech/sherpa_onnx_asr_backend.cpp) | SenseVoice 离线识别适配；配置模型、tokens、语言、ITN、线程、provider；需 EDGE_ENABLE_SHERPA_ASR。 |
| [sherpa_onnx_tts_backend.cpp](../src/speech/sherpa_onnx_tts_backend.cpp) | VITS TTS 适配；配置模型、lexicon/tokens、FST、语速、speaker、线程；需 EDGE_ENABLE_SHERPA_TTS。 |
| [sherpa_onnx_vad_audio_recorder.cpp](../src/speech/sherpa_onnx_vad_audio_recorder.cpp) | Silero VAD 消费 PCM；概率阈值、静音/语音持续时间和最大等待可配置；需 EDGE_ENABLE_SHERPA_VAD。 |

### transport

| 文件 | 职责 |
| --- | --- |
| [llm_control_server.cpp](../src/transport/llm_control_server.cpp) | 独立 REP 工作线程处理取消消息；析构停止并回收线程，服务对象须先于它存活。 |
| [llm_protocol.cpp](../src/transport/llm_protocol.cpp) | LLM v1 generate/generation_result 与流式事件；与 RAG 的 query/事件类型不同。 |
| [llm_service.cpp](../src/transport/llm_service.cpp) | 非流式服务逻辑：解析/校验请求，调用 generate，统计耗时并返回编码 JSON；传输 socket 在应用入口。 |
| [llm_stream_service.cpp](../src/transport/llm_stream_service.cpp) | 将 generateStream 转为编码事件；维护活动 ID，使控制线程能定位取消；不是任意多请求调度器。 |
| [rag_client_zmq.cpp](../src/transport/rag_client_zmq.cpp) | 保留现有拼写的早期字符串 RAG 客户端，返回原始响应；新代码优先使用统一请求器/流式接口。 |
| [rag_control_client.cpp](../src/transport/rag_control_client.cpp) | 独立控制通道发送 cancel 并校验回应，默认 2000ms。 |
| [rag_control_protocol.cpp](../src/transport/rag_control_protocol.cpp) | v1 cancel/cancel_result 编解码，校验空 ID 和 ok/cancelled/error 一致性。 |
| [rag_response_parser.cpp](../src/transport/rag_response_parser.cpp) | 从旧 Python RAG JSON 提取 generated_answer，错误文本和非 JSON 有兼容处理；不是严格协议校验器。 |
| [rag_stream_client.cpp](../src/transport/rag_stream_client.cpp) | DEALER 客户端发送 RAG 流式 JSON，校验 request_id/sequence、积累回答、传递事件，默认等待 120000ms。 |
| [rag_stream_protocol.cpp](../src/transport/rag_stream_protocol.cpp) | RAG v1 请求与 chunk/finished/error 严格编解码；非法字段或事件抛异常。 |
| [zmq_text_client.cpp](../src/transport/zmq_text_client.cpp) | 可注入的文本请求接口及 ZeroMQ REQ 实现，返回 BackendResult；网络超时单位毫秒。 |

### voice

| 文件 | 职责 |
| --- | --- |
| [continuous_voice_session.cpp](../src/voice/continuous_voice_session.cpp) | 连续录音和回答，打断后保留新录音；回调 WaitingForSpeech/RecognizedText/AnswerCompleted/BargeInDetected/Error，结果含轮次与打断计数。 |
| [streaming_sentence_buffer.cpp](../src/voice/streaming_sentence_buffer.cpp) | append 返回新完整句子；flush 返回末尾未完句并清空；不是线程安全容器，pendingBytes 为 UTF-8 字节数。 |
| [voice_assistant.cpp](../src/voice/voice_assistant.cpp) | 装配回答、分句、TTS、播放；processText 等待整轮完成；返回 VoiceAssistantResult 含回答、策略、计数、时序；打断返回独立结果。 |
| [voice_input_pipeline.cpp](../src/voice/voice_input_pipeline.cpp) | 单轮录音→音频校验→ASR→回答；结果含 recognized_text、采集/识别耗时和 assistant_result。 |

## Python 模块与入口

类/函数列表是模块顶层可定位入口，不意味着每个函数都是稳定外部 API；类方法的参数与返回注解以链接源码为准。CLI 用 --help 查看当前选项。

### rag

| 文件 | 职责 | 类 / 顶层函数 |
| --- | --- | --- |
| [__init__.py](../python/rag/__init__.py) | RAG 包标识。 | — |
| [audio_player.py](../python/rag/audio_player.py) | 播放结果、设备解析和可停止音频播放器。 | `PlaybackResult`、`parse_output_device`、`list_output_devices`、`AudioPlayer` |
| [barge_in_detector.py](../python/rag/barge_in_detector.py) | 播放期间的起声检测与打断结果。 | `BargeInResult`、`BargeInDetector` |
| [benchmark_sherpa_tts.py](../python/rag/benchmark_sherpa_tts.py) | 独立 TTS 文本/语音合成基准。 | `main` |
| [benchmark_voice_pipeline.py](../python/rag/benchmark_voice_pipeline.py) | TTS 和完整语音链路的耗时、音频信息及统计。 | `round_ms`、`build_statistics`、`get_audio_info`、`benchmark_tts_case`、`benchmark_full_pipeline`、`main` |
| [build_index.py](../python/rag/build_index.py) | 读取车辆手册与 aliases，构造并保存 chunks JSON。 | `load_manual_lines`、`load_aliases`、`build_retrieval_text`、`build_chunks`、`save_chunks`、`main` |
| [check_env.py](../python/rag/check_env.py) | 检查 Python 运行环境和依赖。 | `main` |
| [debug_tokenize.py](../python/rag/debug_tokenize.py) | 打印分词结果用于检索调试。 | `tokenize`、`main` |
| [list_audio_devices.py](../python/rag/list_audio_devices.py) | 枚举输入/输出音频设备。 | `main` |
| [llm_generator.py](../python/rag/llm_generator.py) | GenerationResult、RAG prompt 与 Mock/Ollama/ZMQ 生成器工厂。 | `GenerationResult`、`MockLLMGenerator`、`OllamaGenerator`、`ZmqLLMGenerator`、`build_rag_prompt`、`create_llm_generator` |
| [llm_zmq_client.py](../python/rag/llm_zmq_client.py) | LLM v1 请求、流式事件和取消客户端。 | `LlmZmqResult`、`LlmZmqStreamEvent`、`LlmZmqClient` |
| [llm_zmq_server.py](../python/rag/llm_zmq_server.py) | Python 非流式 ZeroMQ LLM 服务。 | `LlmZmqServer`、`main` |
| [microphone_io.py](../python/rag/microphone_io.py) | 麦克风设备解析、固定时长录音与录音结果。 | `RecordingResult`、`list_input_devices`、`parse_device`、`MicrophoneRecorder` |
| [python_rag_server.py](../python/rag/python_rag_server.py) | TF-IDF 检索与生成服务，提供普通/流式/取消入口。 | `result_to_dict`、`build_answer`、`build_success_response`、`build_error_response`、`response_to_json`、`PythonRagServer`、`main` |
| [rag_control_server.py](../python/rag/rag_control_server.py) | 独立 RAG 取消 REP 服务及取消传播。 | `RagControlServer` |
| [rag_stream_client.py](../python/rag/rag_stream_client.py) | Python RAG 流式客户端与事件结果。 | `RagStreamEvent`、`RagStreamClient` |
| [rag_stream_protocol.py](../python/rag/rag_stream_protocol.py) | RAG v1 流式请求/事件数据结构和 JSON 编解码。 | `RagStreamRequest`、`RagStreamEvent`、`decode_rag_stream_request`、`encode_rag_stream_event` |
| [run_audio_player.py](../python/rag/run_audio_player.py) | 播放器 CLI 演示。 | `main` |
| [run_faster_whisper_asr.py](../python/rag/run_faster_whisper_asr.py) | Faster-Whisper 文件识别 CLI。 | `main` |
| [run_microphone_record.py](../python/rag/run_microphone_record.py) | 麦克风录音 CLI。 | `main` |
| [run_rag_stream_test.py](../python/rag/run_rag_stream_test.py) | RAG 流式链路 CLI 验证。 | `main` |
| [run_sherpa_onnx_tts.py](../python/rag/run_sherpa_onnx_tts.py) | sherpa TTS CLI 演示。 | `main` |
| [run_vad_record.py](../python/rag/run_vad_record.py) | 基于 VAD 的录音 CLI。 | `main` |
| [run_voice_assistant.py](../python/rag/run_voice_assistant.py) | Python 连续语音助手配置和启动入口。 | `main` |
| [search.py](../python/rag/search.py) | 单次查询、检索/生成和 JSON 输出 CLI。 | `result_to_dict`、`build_answer`、`build_success_response`、`build_error_response`、`print_json_response`、`main` |
| [streaming_sentence_buffer.py](../python/rag/streaming_sentence_buffer.py) | 按分隔符增量输出完整句子。 | `StreamingSentenceBuffer` |
| [test_llm_generator.py](../python/rag/test_llm_generator.py) | 生成器手工冒烟脚本（不同于 python/tests）。 | `main` |
| [test_voice_io.py](../python/rag/test_voice_io.py) | ASR/TTS 手工冒烟脚本。 | `main` |
| [tfidf_search.py](../python/rag/tfidf_search.py) | 中文 TF-IDF 搜索、SearchResult 与 CLI。 | `SearchResult`、`TfidfRagSearcher`、`print_results`、`main` |
| [tts_text.py](../python/rag/tts_text.py) | 播报文本规范化、长句拆分。 | `normalize_tts_text`、`split_tts_text` |
| [vad_recorder.py](../python/rag/vad_recorder.py) | Silero VAD 麦克风语音段录制。 | `VadRecordingResult`、`VadMicrophoneRecorder` |
| [voice_assistant.py](../python/rag/voice_assistant.py) | Python 语音助手状态机、配置和交互控制。 | `AssistantState`、`VoiceAssistantConfig`、`VoiceAssistant` |
| [voice_io.py](../python/rag/voice_io.py) | ASR/TTS 抽象、Mock/FasterWhisper/Sherpa实现及工厂。 | `AsrResult`、`TtsResult`、`AsrBackend`、`TtsBackend`、`MockAsr`、`FasterWhisperAsr`、`MockTts`、`SherpaOnnxTts`、`create_asr_backend`、`create_tts_backend` |
| [voice_pipeline.py](../python/rag/voice_pipeline.py) | ZeroMQ RAG 调用与单轮语音 CLI、JSON 结果。 | `RagClientResult`、`ZmqRagClient`、`build_voice_pipeline_response`、`build_error_response`、`print_json`、`main` |

### embedding

| 文件 | 职责 | 类 / 顶层函数 |
| --- | --- | --- |
| [__init__.py](../python/embedding/__init__.py) | Embedding 包标识。 | — |
| [build_dense_index.py](../python/embedding/build_dense_index.py) | 编码手册为归一化向量，写入 f32 和 v2 元数据/指纹。 | `sha256_file`、`fnv1a64_file`、`build_retrieval_text`、`load_chunks`、`build_session_inputs`、`encode_documents`、`write_binary_embeddings`、`build_index`、`main` |
| [export_bge_onnx.py](../python/embedding/export_bge_onnx.py) | 包装 BGE CLS pooling/归一化，导出 ONNX 并对齐 PyTorch/ORT 输出。 | `BgeEmbeddingModel`、`build_example_inputs`、`validate_onnx_model`、`export_model`、`main` |
| [verify_dense_index.py](../python/embedding/verify_dense_index.py) | 校验模型、tokenizer、chunks、向量文件和元数据一致性。 | `sha256_file`、`fnv1a64_file`、`load_json`、`verify_index`、`main` |

### agent_finetune

| 文件 | 职责 | 类 / 顶层函数 |
| --- | --- | --- |
| [__init__.py](../python/agent_finetune/__init__.py) | Planner 训练工具包标识。 | — |
| [check_base_model.py](../python/agent_finetune/check_base_model.py) | 加载基础模型，生成工具动作并评估格式/资源使用。 | `gibibytes`、`print_gpu_memory`、`load_model`、`generate_action`、`evaluate_output`、`main` |
| [evaluate_planner.py](../python/agent_finetune/evaluate_planner.py) | 基础模型/adapter离线评测，比较工具动作、参数与回答关键词。 | `load_jsonl`、`load_model`、`generate`、`compare_action`、`main` |
| [generate_atomic_condition_compat.py](../python/agent_finetune/generate_atomic_condition_compat.py) | 构造原子条件控制的兼容性数据。 | `atomic_arguments`、`add_record`、`atomic_result`、`build_records`、`main` |
| [generate_condition_compat.py](../python/agent_finetune/generate_condition_compat.py) | 构造条件判断工具的兼容性数据。 | `add_record`、`condition_arguments`、`build_records`、`main` |
| [generate_dataset.py](../python/agent_finetune/generate_dataset.py) | 初版环境查询、设备状态、控制与条件任务数据。 | `environment_records`、`state_records`、`control_records`、`conditional_records`、`build_records`、`write_jsonl`、`main` |
| [generate_dataset_v2.py](../python/agent_finetune/generate_dataset_v2.py) | v2 数据及元数据、训练/验证划分。 | `belongs_to_validation`、`add_metadata`、`build_environment_records`、`build_state_records`、`build_control_records`、`build_conditional_records`、`build_records`、`write_jsonl`、`print_distribution`、`main` |
| [generate_dataset_v3.py](../python/agent_finetune/generate_dataset_v3.py) | v3 原子条件数据、分组划分及拆分校验。 | `AtomicConditionSpec`、`build_atomic_specs`、`atomic_arguments`、`matched_temperature`、`unmatched_temperature`、`atomic_result`、`add_atomic_metadata`、`build_atomic_condition_records`、`atomic_group_is_validation`、`record_is_validation`、`validate_split`、`write_jsonl`、`print_distribution`、`build_records`、`split_records`、`main` |
| [generate_heldout.py](../python/agent_finetune/generate_heldout.py) | 构造独立 heldout 场景。 | `build_records`、`main` |
| [planner_format.py](../python/agent_finetune/planner_format.py) | 统一动作构造、schema/严格解析、prompt与 Observation记录。 | `ValidationResult`、`canonical_json`、`tool_call`、`final_answer`、`validate_action`、`parse_strict_action`、`build_runtime_prompt`、`make_record`、`observation` |
| [train_qlora.py](../python/agent_finetune/train_qlora.py) | SFT dataset/collator、4-bit加载+LoRA训练、adapter与指标保存。 | `PlannerSftDataset`、`PlannerDataCollator`、`parse_args`、`set_seed`、`main` |
| [validate_dataset.py](../python/agent_finetune/validate_dataset.py) | 逐记录/文件检查 Planner 数据格式和动作。 | `validate_record`、`validate_file`、`main` |

## 联调脚本

脚本在项目根目录运行；部分脚本固定 conda 环境名、路径、端口或依赖后台服务，先阅读后执行。

| 文件 | 用途 |
| --- | --- |
| [build_bge_dense_index.sh](../scripts/build_bge_dense_index.sh) | 构建 bge dense index 资源/索引；具体参数见脚本 |
| [build_python_index.sh](../scripts/build_python_index.sh) | 构建 python index 资源/索引；具体参数见脚本 |
| [check_ollama.sh](../scripts/check_ollama.sh) | 检查 ollama 环境/服务；具体参数见脚本 |
| [check_python_env.sh](../scripts/check_python_env.sh) | 检查 python env 环境/服务；具体参数见脚本 |
| [run_cpp_llm_server.sh](../scripts/run_cpp_llm_server.sh) | 启动 cpp llm server 服务或语音入口；具体参数见脚本 |
| [run_llm_server.sh](../scripts/run_llm_server.sh) | 启动 llm server 服务或语音入口；具体参数见脚本 |
| [run_python_rag_server.sh](../scripts/run_python_rag_server.sh) | 启动 python rag server 服务或语音入口；具体参数见脚本 |
| [run_python_rag_server_debug.sh](../scripts/run_python_rag_server_debug.sh) | 启动 python rag server debug 服务或语音入口；具体参数见脚本 |
| [run_python_rag_server_ollama.sh](../scripts/run_python_rag_server_ollama.sh) | 启动 python rag server ollama 服务或语音入口；具体参数见脚本 |
| [run_voice_assistant.sh](../scripts/run_voice_assistant.sh) | 启动 voice assistant 服务或语音入口；具体参数见脚本 |
| [run_voice_pipeline_mock.sh](../scripts/run_voice_pipeline_mock.sh) | 启动 voice pipeline mock 服务或语音入口；具体参数见脚本 |
| [search_python_json.sh](../scripts/search_python_json.sh) | 执行 python json 查询；具体参数见脚本 |
| [search_python_tfidf.sh](../scripts/search_python_tfidf.sh) | 执行 python tfidf 查询；具体参数见脚本 |
| [test_cpp_python_zmq_backend.sh](../scripts/test_cpp_python_zmq_backend.sh) | 联调/冒烟验证 cpp python zmq backend；具体参数见脚本 |
| [test_mock_llm.sh](../scripts/test_mock_llm.sh) | 联调/冒烟验证 mock llm；具体参数见脚本 |
| [test_ollama_manual.sh](../scripts/test_ollama_manual.sh) | 联调/冒烟验证 ollama manual；具体参数见脚本 |
| [test_once_queries.sh](../scripts/test_once_queries.sh) | 联调/冒烟验证 once queries；具体参数见脚本 |
| [test_python_json_search.sh](../scripts/test_python_json_search.sh) | 联调/冒烟验证 python json search；具体参数见脚本 |
| [test_python_rag_server.sh](../scripts/test_python_rag_server.sh) | 联调/冒烟验证 python rag server；具体参数见脚本 |
| [test_python_rag_server_debug_prompt.sh](../scripts/test_python_rag_server_debug_prompt.sh) | 联调/冒烟验证 python rag server debug prompt；具体参数见脚本 |
| [test_python_tfidf.sh](../scripts/test_python_tfidf.sh) | 联调/冒烟验证 python tfidf；具体参数见脚本 |
| [test_voice_io.sh](../scripts/test_voice_io.sh) | 联调/冒烟验证 voice io；具体参数见脚本 |
| [test_voice_pipeline_mock.sh](../scripts/test_voice_pipeline_mock.sh) | 联调/冒烟验证 voice pipeline mock；具体参数见脚本 |
| [test_zmq_backend.sh](../scripts/test_zmq_backend.sh) | 联调/冒烟验证 zmq backend；具体参数见脚本 |

## 工具、配置与驱动

| 文件 | 用途 |
| --- | --- |
| [retrieval_benchmark.cpp](../tools/retrieval_benchmark.cpp) | 检索质量/负例拒答/耗时；--samples输出run/case原始时延，仍输出METRICS_JSON |
| [bge_retrieval_profile.cpp](../tools/bge_retrieval_profile.cpp) | BGE开启时构建；显式采样准备/ORT/后处理/索引/映射，输出JSONL |
| [run_retrieval_baseline.py](../tools/run_retrieval_baseline.py) | 标准库离线记录器；固定输入/指纹、原始样本、GNU/BusyBox time原生RSS与失败清单 |
| [voice_performance_summary.cpp](../tools/voice_performance_summary.cpp) | 读取 PERF_JSON，输出 P50/P95、平均值、最大值和日志计数 |
| [app.conf](../config/app.conf) | 文本客户端 AppConfig 配置 |
| [board_pc_mock.conf](../config/board_pc_mock.conf) | 板端/PC/WSL语音 BoardAppConfig 配置 |
| [board_rk3588.conf](../config/board_rk3588.conf) | 板端/PC/WSL语音 BoardAppConfig 配置 |
| [board_wsl.conf](../config/board_wsl.conf) | 板端/PC/WSL语音 BoardAppConfig 配置 |
| [dev.conf](../config/dev.conf) | 文本客户端 AppConfig 配置 |
| [python_zmq.conf](../config/python_zmq.conf) | 文本客户端 AppConfig 配置 |
| [zmq.conf](../config/zmq.conf) | 文本客户端 AppConfig 配置 |
| [edge_dht11.c](../kernel_modules/edge_dht11/edge_dht11.c) | GPIO双边沿IRQ、脉宽解码、校验和、缓存、IIO注册与读取 |
| [edge_dht11.h](../kernel_modules/edge_dht11/edge_dht11.h) | 驱动时序常量和帧结构 |
| [Makefile](../kernel_modules/edge_dht11/Makefile) | 独立板端内核模块构建 |
| [rk3588-buildroot.cmake](../cmake/toolchains/rk3588-buildroot.cmake) | 定位Buildroot host并加载其toolchainfile |
| [retrieval_cases.json](../benchmarks/retrieval_cases.json) | 正/负例查询、相关chunk_id和类别 |

## 回归测试文件

逐项测试功能、受控生命周期与压力运行方法见[C++测试手册](testing-guide.md)。

默认/可选 C++ 测试的注册条件见 [tests/CMakeLists.txt](../tests/CMakeLists.txt)，整体构建组织见[CMake指南](cmake-structure.md)。同名模块通常对应相应接口；workflow覆盖多步确认，protocol覆盖错误消息，integration/consistency需要模型资产。Python 测试不默认访问真实音频或训练GPU。

### tests

- [agent_answer_backend_tests.cpp](../tests/agent_answer_backend_tests.cpp)
- [agent_executor_tests.cpp](../tests/agent_executor_tests.cpp)
- [agent_routing_answer_backend_tests.cpp](../tests/agent_routing_answer_backend_tests.cpp)
- [agent_workflow_tests.cpp](../tests/agent_workflow_tests.cpp)
- [alsa_audio_backend_tests.cpp](../tests/alsa_audio_backend_tests.cpp)
- [bge_embedder_tests.cpp](../tests/bge_embedder_tests.cpp)
- [bm25_retriever_tests.cpp](../tests/bm25_retriever_tests.cpp)
- [board_app_config_tests.cpp](../tests/board_app_config_tests.cpp)
- [bounded_blocking_queue_tests.cpp](../tests/bounded_blocking_queue_tests.cpp)
- [chinese_text_tokenizer_tests.cpp](../tests/chinese_text_tokenizer_tests.cpp)
- [continuous_voice_session_lifecycle_tests.cpp](../tests/continuous_voice_session_lifecycle_tests.cpp)
- [continuous_voice_session_tests.cpp](../tests/continuous_voice_session_tests.cpp)
- [dense_index_consistency_tests.cpp](../tests/dense_index_consistency_tests.cpp)
- [dense_retriever_tests.cpp](../tests/dense_retriever_tests.cpp)
- [dense_vector_index_tests.cpp](../tests/dense_vector_index_tests.cpp)
- [edge_response_backend_tests.cpp](../tests/edge_response_backend_tests.cpp)
- [file_fingerprint_tests.cpp](../tests/file_fingerprint_tests.cpp)
- [hybrid_retriever_integration_tests.cpp](../tests/hybrid_retriever_integration_tests.cpp)
- [hybrid_retriever_tests.cpp](../tests/hybrid_retriever_tests.cpp)
- [knowledge_base_loader_tests.cpp](../tests/knowledge_base_loader_tests.cpp)
- [linux_vehicle_device_tests.cpp](../tests/linux_vehicle_device_tests.cpp)
- [llm_agent_planner_tests.cpp](../tests/llm_agent_planner_tests.cpp)
- [llm_backend_factory_tests.cpp](../tests/llm_backend_factory_tests.cpp)
- [llm_backend_stream_tests.cpp](../tests/llm_backend_stream_tests.cpp)
- [llm_control_server_tests.cpp](../tests/llm_control_server_tests.cpp)
- [llm_protocol_tests.cpp](../tests/llm_protocol_tests.cpp)
- [llm_service_tests.cpp](../tests/llm_service_tests.cpp)
- [llm_stream_service_tests.cpp](../tests/llm_stream_service_tests.cpp)
- [local_rag_llm_backend_tests.cpp](../tests/local_rag_llm_backend_tests.cpp)
- [multi_level_response_tests.cpp](../tests/multi_level_response_tests.cpp)
- [policy_routing_answer_backend_tests.cpp](../tests/policy_routing_answer_backend_tests.cpp)
- [rag_control_client_tests.cpp](../tests/rag_control_client_tests.cpp)
- [rag_control_protocol_tests.cpp](../tests/rag_control_protocol_tests.cpp)
- [rag_stream_client_tests.cpp](../tests/rag_stream_client_tests.cpp)
- [rag_stream_protocol_tests.cpp](../tests/rag_stream_protocol_tests.cpp)
- [relevance_filtering_retriever_tests.cpp](../tests/relevance_filtering_retriever_tests.cpp)
- [retriever_runtime_tests.cpp](../tests/retriever_runtime_tests.cpp)
- [sherpa_onnx_asr_backend_tests.cpp](../tests/sherpa_onnx_asr_backend_tests.cpp)
- [sherpa_onnx_tts_backend_tests.cpp](../tests/sherpa_onnx_tts_backend_tests.cpp)
- [sherpa_onnx_vad_audio_recorder_tests.cpp](../tests/sherpa_onnx_vad_audio_recorder_tests.cpp)
- [streaming_answer_backend_tests.cpp](../tests/streaming_answer_backend_tests.cpp)
- [streaming_sentence_buffer_tests.cpp](../tests/streaming_sentence_buffer_tests.cpp)
- [unit_tests.cpp](../tests/unit_tests.cpp)
- [vehicle_tools_tests.cpp](../tests/vehicle_tools_tests.cpp)
- [voice_assistant_tests.cpp](../tests/voice_assistant_tests.cpp)
- [voice_input_pipeline_tests.cpp](../tests/voice_input_pipeline_tests.cpp)
- [voice_lifecycle_tests.cpp](../tests/voice_lifecycle_tests.cpp)（含--stress入口）
- [support/voice_test_support.h](../tests/support/voice_test_support.h)（测试检查点、可控后端与失败清理）
- [voice_performance_report_tests.cpp](../tests/voice_performance_report_tests.cpp)
- [voice_performance_statistics_tests.cpp](../tests/voice_performance_statistics_tests.cpp)
- [wav_audio_reader_tests.cpp](../tests/wav_audio_reader_tests.cpp)

### python/tests

- [test_agent_dataset_v3.py](../python/tests/test_agent_dataset_v3.py)
- [test_agent_planner_format.py](../python/tests/test_agent_planner_format.py)
- [test_audio_player.py](../python/tests/test_audio_player.py)
- [test_barge_in_detector.py](../python/tests/test_barge_in_detector.py)
- [test_llm_zmq_client.py](../python/tests/test_llm_zmq_client.py)
- [test_microphone_io.py](../python/tests/test_microphone_io.py)
- [test_python_rag_server_control.py](../python/tests/test_python_rag_server_control.py)
- [test_rag_control_server.py](../python/tests/test_rag_control_server.py)
- [test_rag_stream_client.py](../python/tests/test_rag_stream_client.py)
- [test_rag_stream_protocol.py](../python/tests/test_rag_stream_protocol.py)
- [test_streaming_sentence_buffer.py](../python/tests/test_streaming_sentence_buffer.py)
- [test_tts_text.py](../python/tests/test_tts_text.py)
- [test_vad_recorder.py](../python/tests/test_vad_recorder.py)
- [test_voice_io.py](../python/tests/test_voice_io.py)
- [test_voice_pipeline.py](../python/tests/test_voice_pipeline.py)

## 本地待提交的评测工作

以下内容保留在工作区，不属于本次目录/CMake/文档整理提交；新克隆无需存在这些文件才能浏览本索引：

- `python/agent_finetune/evaluate_planner.py` 的分组汇总、回答词组等评测改动。
- `python/agent_finetune/generate_robustness_heldout_v3.py` 和 `data/agent_planner/robustness_heldout_v3.jsonl`。
- `python/tests/test_agent_evaluate_planner.py` 和 `python/tests/test_agent_robustness_heldout_v3.py`。

离线基准记录器回归：[test_retrieval_baseline.py](../tests/test_retrieval_baseline.py)，用Python标准库unittest单独执行，不属于CTest条目。

[prepare_rk3588_perf.py](../tools/prepare_rk3588_perf.py)：生成新版本ARM部署包、递归动态库依赖与交叉构建清单；拒绝覆盖输出。硬件接线见[DHT11](hardware-dht11.md)，操作流程见[RK3588性能](rk3588-performance.md)。

DHT11新增共享帧解析器[edge_dht11_frame.h](../kernel_modules/edge_dht11/edge_dht11_frame.h)与[模块测试说明](../kernel_modules/edge_dht11/README.md)：用于内核/Host复用同一解码实现并回放真实采样。

[dht11_latency_probe.py](../tools/dht11_latency_probe.py)：板端手动诊断，比较普通读取和临时CPU延迟请求，输出JSONL样本/汇总；文件描述符关闭后释放请求，不修改驱动或governor。

DHT11接口到硬件的调用入口及历次修订，见[驱动专题图解](dht11-driver-guide.md)；图源与SVG索引见[diagrams](diagrams/README.md)。

[agent_demo.cpp](../src/apps/agent_demo.cpp)新增Mock/IIO后端选择和IIO根目录/name参数，沿用既有Rule/工具/确认执行器；[联合设备测试](../tests/linux_vehicle_device_tests.cpp)覆盖IIO到Agent的查询、确认/取消、错误与恢复。运行方式见[板端联调](board-agent-bringup.md)。

板端私有ALSA配置[alsa-rk3588.conf](../config/alsa-rk3588.conf)固定底层16k；通过启动环境加载，不修改系统配置。RuleAgentPlanner复用AgentIntentClassifier的EnvironmentQuery分类，避免口语问句已路由却未执行工具。见[修复说明](rk3588-duplex-fix.md)。


### TTS延迟优化增量

| 文件 | 功能 |
| --- | --- |
| [board_tts_config.h](../include/board_tts_config.h)、[映射实现](../src/config/board_tts_config.cpp) | 共用BoardAppConfig→Sherpa配置，不创建引擎，旧conf兼容 |
| [tts_benchmark.cpp](../src/apps/tts_benchmark.cpp) | 固定合成输入、模型/线程覆盖、加载/首调用/预热/RTF/RSS与JSONL |
| [tts_cases.json](../benchmarks/tts_cases.json) | 数值原句、中文数词、确认语句与手册短答 |
| [低延迟备选配置](../config/board_rk3588_tts_fast.conf) | AISHELL3 8k speaker66；包安装时继承用户主conf的其他参数 |

优化结果与操作见[TTS手册](tts-optimization.md)；未来ASR/TTS NPU范围见[迁移路线](speech-npu-roadmap.md)。
