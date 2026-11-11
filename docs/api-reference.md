# C++ 接口参考

覆盖当前 `include/` 中全部公共头文件。表中省略重复的析构、配置访问器参数类型和 success/failure 工厂细节；完整签名点击头文件查看。接口按职责分组，不代表头文件已经移动目录。

## 通用调用契约

- `ok=false` 时读取 `error`，不把空文本/空数组当作有效业务答案；采用 `load()+lastError()` 的接口必须先检查装载结果。
- 配置验证、协议解析、SDK 初始化、WAV/IIO 文件访问可能抛异常；应用入口负责捕获。不同旧接口错误策略并不完全统一。
- 传入的后端、设备、注册表等引用均为非拥有引用，必须覆盖使用者生命周期；`unique_ptr` 表示所有权移交。
- `const` 不代表无共享状态或任意并发安全。当前整体按单会话、单活动回答设计；取消可来自另一线程，不能据此推导并发 generate/run 安全。
- 音频为 PCM16；文本为 UTF-8。时间默认毫秒，但 Python CLI `--llm-timeout` 是秒；温度摄氏度、湿度 %RH。
- 流式回调在当前调用路径执行；不要假定是 UI 线程，也不要保存事件对象的临时引用。

## 配置与公共工具

| 头文件 | 公开入口 | 功能与约定 |
| --- | --- | --- |
| [app_config.h](../include/app_config.h) | `AppConfig(path); load(); knowledgePath/topK/ragBackend/ragEndpoint/ragTimeoutMs/llmEndpoint/llmTimeoutMs` | 文本 CLI 配置。load 返回 bool；读取手册路径、远程端口和超时，不承担语音模型配置。 |
| [board_app_config.h](../include/board_app_config.h) | `BoardAppConfig::load(path); validate(); 配置字段` | 板端语音/检索/Agent配置；无效文件或值抛异常；默认值与实际 conf 覆盖值应区分。 |
| [command_line_options.h](../include/command_line_options.h) | `CommandLineOptions::parse(argc, argv); usage(); configPath/onceQuery/showHelp/hasError/onceMode/errorMessage` | 解析文本 CLI 的 --config、--once、--help；错误保存在对象内供入口决定退出。 |
| [logger.h](../include/logger.h) | `Logger::log(LogLevel, message)` | 静态日志输出，区分 Info/Warning/Error/User/Route/System/Perf。 |
| [perf_timer.h](../include/perf_timer.h) | `PerfTimer(name); reset(); elapsedMilliseconds(); name()` | steady_clock 计时器，单位毫秒；用于阶段耗时，不是 CPU 时间计数器。 |
| [file_fingerprint.h](../include/file_fingerprint.h) | `calculateFileFnv1a64(path) → FileFingerprintResult` | 读取原始字节，返回固定 16 位小写十六进制指纹或 error；用于一致性检查，不是密码学完整性验证。 |
| [scope_exit.h](../include/scope_exit.h) | `ScopeExit<Function>; makeScopeExit(fn); release()` | 作用域退出调用清理函数，release 放弃清理；禁止复制，清理函数应不抛异常。 |
| [bounded_blocking_queue.h](../include/bounded_blocking_queue.h) | `BoundedBlockingQueue<T>(capacity); push(const T&/T&&); push(value,QueuePushMetrics&); pop(); close(); clear(); size/empty/capacity/closed` | 容量必须大于零；push 满时等待、关闭后返回 false；pop 空时等待、关闭且耗尽返回 nullopt；clear 返回移除数，close 不可逆；观测重载返回容量等待和成功push的锁内共享深度，原重载不采时。 |

## 音频、语音模型与会话

| 头文件 | 公开入口 | 功能与约定 |
| --- | --- | --- |
| [audio_buffer.h](../include/audio_buffer.h) | `AudioBuffer.samples/sample_rate/channels; empty(); frameCount(); durationSeconds()` | PCM16 交错采样；samples.size 是采样值个数，frameCount 除以声道数，durationSeconds 再除采样率。 |
| [audio_recorder.h](../include/audio_recorder.h) | `AudioRecorder::name/recordUtterance/stop; AudioCaptureResult` | 阻塞获得一段音频；返回 ok/audio/error/elapsed_ms；stop 请求停止录音器。 |
| [interruptible_audio_recorder.h](../include/interruptible_audio_recorder.h) | `InterruptibleAudioRecorder::recordUtterance(SpeechStartedHandler); cancelCurrentRecording()` | 起声时调用 void() 回调，继续收集该句话；cancelCurrentRecording 只取消本轮，与终止 stop 区分。 |
| [pcm_stream_source.h](../include/pcm_stream_source.h) | `PcmStreamSource::name/sampleRate/channels/capture/cancelCurrentCapture/stop; PcmChunkHandler` | capture 逐块调用 bool(const AudioBuffer&)；false 请求结束采集；返回 PcmStreamResult，captured_frames 是帧数。 |
| [audio_player.h](../include/audio_player.h) | `AudioPlayer::name/play(AudioBuffer)/stop; AudioPlaybackResult` | 阻塞播放，结果为 ok/error；stop 用于其他线程停止当前输出，具体并发行为由适配器实现。 |
| [asr_backend.h](../include/asr_backend.h) | `AsrBackend::name/transcribe(AudioBuffer); AsrTranscriptionResult` | PCM → text，返回 ok/text/error/elapsed_ms；ASR 结果仍可能为空，上层必须校验。 |
| [tts_backend.h](../include/tts_backend.h) | `TtsBackend::name/synthesize(text); TtsSynthesisResult` | 文本 → AudioBuffer；返回 ok/audio/error，不保证所有实现均支持合成中硬取消。 |
| [alsa_audio_player.h](../include/alsa_audio_player.h) | `AlsaAudioPlayer(config); name/play/stop/config` | ALSA 播放适配，device 与 period_frames 可配置；RAII + PImpl，禁复制；需 EDGE_ENABLE_ALSA。play失败error附请求device、sample_rate、channels、period_frames_requested；这些不是硬件协商实值。 |
| [alsa_audio_recorder.h](../include/alsa_audio_recorder.h) | `AlsaAudioRecorder(config); name/recordUtterance/stop/config` | 固定时长 ALSA 录音，record_duration_ms/采样率/声道/period 可配置；需 EDGE_ENABLE_ALSA。 |
| [alsa_pcm_stream_source.h](../include/alsa_pcm_stream_source.h) | `AlsaPcmStreamSource(config); name/sampleRate/channels/capture/cancelCurrentCapture/stop/config` | 连续 PCM chunk 源；默认 16000Hz、单声道、512 帧与 100000us 延迟；需 EDGE_ENABLE_ALSA。 |
| [wav_audio_reader.h](../include/wav_audio_reader.h) | `WavAudioReader::read(path) → AudioBuffer` | 解析 RIFF/WAVE 和 PCM 数据，跳过支持的可选块；不支持或损坏输入抛异常；不是任意音频格式解码器。 |
| [wav_audio_recorder.h](../include/wav_audio_recorder.h) | `WavAudioRecorder(path); name/recordUtterance/stop/path` | 把 WAV 当作录音输入供离线流水线使用；停止状态通过 atomic 保存。 |
| [sherpa_onnx_asr_backend.h](../include/sherpa_onnx_asr_backend.h) | `SherpaOnnxAsrBackend(config); name/transcribe/config` | SenseVoice 离线识别适配；配置模型、tokens、语言、ITN、线程、provider；需 EDGE_ENABLE_SHERPA_ASR。 |
| [sherpa_onnx_tts_backend.h](../include/sherpa_onnx_tts_backend.h) | `SherpaOnnxTtsBackend(config); name/synthesize/config` | VITS TTS 适配；配置模型、lexicon/tokens、FST、语速、speaker、线程；需 EDGE_ENABLE_SHERPA_TTS。 |
| [sherpa_onnx_vad_audio_recorder.h](../include/sherpa_onnx_vad_audio_recorder.h) | `SherpaOnnxVadAudioRecorder(source, config); name/recordUtterance/cancelCurrentRecording/stop/config` | Silero VAD 消费 PCM；概率阈值、静音/语音持续时间和最大等待可配置；需 EDGE_ENABLE_SHERPA_VAD。 |
| [streaming_sentence_buffer.h](../include/streaming_sentence_buffer.h) | `StreamingSentenceBuffer(delimiters); append(delta); flush(); clear(); empty/pendingBytes/pendingText` | append 返回新完整句子；flush 返回末尾未完句并清空；不是线程安全容器，pendingBytes 为 UTF-8 字节数。 |
| [voice_assistant.h](../include/voice_assistant.h) | `VoiceAssistant(backends, capacities); start/stop/running; processText(id, query); stopPlayback()` | 装配回答、分句、TTS、播放；processText 等待整轮完成；返回 VoiceAssistantResult 含回答、策略、计数、时序；打断返回独立结果。 |
| [voice_input_pipeline.h](../include/voice_input_pipeline.h) | `VoiceInputPipeline(recorder, asr, assistant); processOnce(id); stopRecording()` | 单轮录音→音频校验→ASR→回答；结果含 recognized_text、采集/识别耗时和 assistant_result。 |
| [continuous_voice_session.h](../include/continuous_voice_session.h) | `ContinuousVoiceSession(normal, barge, asr, assistant, config); run(handler); stop()` | 连续录音和回答；answer或barge分支取得的成功录音均保留，已起声时等待该语句录完；ASR返回/识别事件回调后检查stop，已观察到停止则不派发回答；回调 WaitingForSpeech/RecognizedText/AnswerCompleted/BargeInDetected/Error，结果含轮次与打断计数。 |
| [voice_performance_report.h](../include/voice_performance_report.h) | `VoicePerformanceReport::KVersion; encode(VoiceSessionEvent)` | 仅接受有效 AnswerCompleted 和一致 request_id；返回单行 JSON 字符串，不含外部 [PERF_JSON] 前缀；非法事件抛异常。 |
| [voice_performance_statistics.h](../include/voice_performance_statistics.h) | `VoicePerformanceAggregator::addLogLine(line,error); summarize(); VoiceMetricStatistics/VoicePerformanceStatistics` | 普通行返回 false 且 error 空，坏报告返回 false 且 error 非空；汇总平均/P50/P95/最大值和有效/失败样本计数；兼容旧报告，只聚合observed且complete的队列等待，计数/深度不混入ms表。 |

## 检索

| 头文件 | 公开入口 | 功能与约定 |
| --- | --- | --- |
| [retrieval_types.h](../include/retrieval_types.h) | `DocumentChunk; RetrievalResult` | chunk_id/title/content/text/aliases/retrieval_text；命中包含 chunk 和 sparse_score/dense_score/final_score。 |
| [retriever.h](../include/retriever.h) | `Retriever::searchTopK(query, int top_k) const → vector<RetrievalResult>` | 统一检索入口；返回排名列表，空列表表示无命中；具体装载及错误策略由实现定义。 |
| [chinese_text_tokenizer.h](../include/chinese_text_tokenizer.h) | `ChineseTextTokenizer::tokenize(text)` | 中文文本分词，供 BM25 构建/查询词项；精确签名见头文件，不使用 Python jieba 实例。 |
| [knowledge_base_loader.h](../include/knowledge_base_loader.h) | `loadDocumentChunks(path) → KnowledgeBaseLoadResult` | 解析 chunks，返回 ok/documents/error；统一手册与别名构造规则。 |
| [bm25_retriever.h](../include/bm25_retriever.h) | `Bm25Retriever(path, config); loadKnowledgeBase(); searchTopK(); documentCount/vocabularySize/averageDocumentLength` | 建立内存倒排统计并按 BM25 排序；配置 k1/b/minimum_matched_terms；应先成功加载。 |
| [bge_embedder.h](../include/bge_embedder.h) | `BgeEmbedder(config); load(); encodeQuery(query); encodeQueryProfiled(query); isLoaded/dimension/lastError` | ONNX Runtime CPU + tokenizers-cpp；返回 ok/embedding/error，默认检索指令、512维和max_length=512；Profiled额外返回锁/准备/ORT/后处理分段和token数，错误时observed=false；需BGE开关。 |
| [dense_vector_index.h](../include/dense_vector_index.h) | `DenseVectorIndex(meta,f32); load(); searchTopK(vector,k); isLoaded/rowCount/dimension/maxLength/chunkIds/modelId/pooling/queryInstruction/lastError/chunksFingerprint` | 载入连续 float32 向量及元数据，查询归一化向量并返回 row/chunk_id/score；负责格式、大小等校验。 |
| [dense_retriever.h](../include/dense_retriever.h) | `DenseRetriever(config); load(); searchTopK(); searchTopKProfiled(query,k,timing); isLoaded/lastError` | 组合 BGE、DenseVectorIndex 与手册映射，检查索引和当前chunks一致性；Profiled拆分编码/索引/映射并先清空计时对象，普通路径不采分段时间；需BGE开关。 |
| [hybrid_retriever.h](../include/hybrid_retriever.h) | `edge_voice_rag::HybridRetriever(sparse,dense,config); searchTopK()` | 非拥有引用组合双路检索；加权 RRF 合并去重；candidate_top_k 控制候选数。 |
| [relevance_filtering_retriever.h](../include/relevance_filtering_retriever.h) | `RelevanceFilteringRetriever(inner,config); searchTopK()` | 装饰器过滤原始 sparse/dense 分数不达标结果，再截取 top_k；不能用 RRF 分数替代原始分数。 |
| [retriever_runtime.h](../include/retriever_runtime.h) | `RetrieverRuntime(config); load(); retriever(); backendName/relevanceFilterEnabled/lastError` | 拥有检索器和过滤器，load 成功后才可访问 retriever 引用；未加载时访问抛 logic_error；关闭 BGE 时拒绝 dense/hybrid。 |

## 响应与推理

| 头文件 | 公开入口 | 功能与约定 |
| --- | --- | --- |
| [query_router.h](../include/query_router.h) | `QueryRouter::classify(query); typeToString(QueryType)` | 早期 VehicleManual/chat/Unknown 分类接口，主要保留给原有调用和测试；不要与 QueryClassifier 混同。 |
| [query_classifier.h](../include/query_classifier.h) | `QueryClassifier::analyze/classify/categoryToString` | 提取关键词、紧急/事实/复杂/创作特征并给类别、confidence、reasoning 和 requires_immediate_response。 |
| [response_backend.h](../include/response_backend.h) | `ResponseBackend::searchRag(query)/generateLlm(prompt); BackendResult` | 非流式旧抽象；返回 ok/text/error。 |
| [edge_response_backend.h](../include/edge_response_backend.h) | `EdgeResponseBackend(retriever,requester,config...); searchRag/generateLlm` | 旧文本系统的适配器；local 走本地检索，远程走 ZeroMQ；LLM 请求经协议编解码。 |
| [multi_level_response_system.h](../include/multi_level_response_system.h) | `MultiLevelResponseSystem(backend,cache_limit); decide/process/clearCache/cacheSize/modeToString` | 旧文本路由与进程内缓存；QueryProcessResult 含 ok/answer/error/category/mode/from_cache。 |
| [response_policy.h](../include/response_policy.h) | `ResponsePolicy(config); decideWithoutRetrieval/decideWithRetrieval/isHighConfidence/modeToString` | 纯策略，返回 decision 包含类别、模式、检索数量、置信判断及 reason；不执行模型。 |
| [streaming_answer_backend.h](../include/streaming_answer_backend.h) | `StreamingAnswerBackend::name/query(RagStreamRequest,handler); RagStreamQueryResult; RagQueryTiming` | 统一本地/远程流式回答；回调接收 RagStreamEvent，返回累计回答、ok/error 和检索/LLM时序，策略元数据可选。 |
| [cancellable_answer_backend.h](../include/cancellable_answer_backend.h) | `CancellableAnswerBackend::name/cancel(request_id); CancellationResult` | 请求取消，返回 ok/cancelled/request_id/active_request_id/error；成功收到响应和实际取消是不同状态。 |
| [local_rag_llm_backend.h](../include/local_rag_llm_backend.h) | `LocalRagLlmBackend(retriever,llm,config); name/query/cancel` | 固定 RAG+LLM 流水线，拼手册 prompt，转发 chunk 并计时；取消仅针对匹配的活动 request_id。 |
| [policy_routing_answer_backend.h](../include/policy_routing_answer_backend.h) | `PolicyRoutingAnswerBackend(retriever,llm,config); name/query/cancel` | 按 ResponsePolicy 执行固定回复、直答、RAG+LLM、纯 LLM 或澄清，结果携带策略原因。 |
| [llm_backend.h](../include/llm_backend.h) | `LlmBackend::name/generate(prompt)/generateStream(prompt,callback)/cancel; LlmGenerationResult` | generate 返回 ok/answer/error；基类 generateStream 默认生成完再回调一次，空 callback 失败；cancel 默认 false，不保证原生流式或可取消。 |
| [llm_backend_factory.h](../include/llm_backend_factory.h) | `LlmBackendOptions; createLlmBackend(options) → unique_ptr<LlmBackend>` | 选择 mock/rkllm；未开启 RKLLM 或不支持配置时拒绝；构造时完成后端初始化。 |
| [mock_llm_backend.h](../include/mock_llm_backend.h) | `MockLlmBackend::name/generate/generateStream/cancel` | 确定性回答与流式联调后端；普通 Mock 输出不满足 Planner 工具 JSON，不能替代 Scripted LLM 或 Rule Planner。 |
| [rkllm_backend.h](../include/rkllm_backend.h) | `RkllmBackend(config); name/generate/generateStream/cancel` | 通过 rkllm_init/run/abort/destroy 管理 SDK handle，回调传递 delta；禁复制，需 EDGE_ENABLE_RKLLM 及匹配模型。 |

## 通信与服务

| 头文件 | 公开入口 | 功能与约定 |
| --- | --- | --- |
| [zmq_text_client.h](../include/zmq_text_client.h) | `TextRequester::request(endpoint,text,timeout_ms); ZmqTextClient::request` | 可注入的文本请求接口及 ZeroMQ REQ 实现，返回 BackendResult；网络超时单位毫秒。 |
| [rag_client_zmq.h](../include/rag_client_zmq.h) | `RagClilentZmq(endpoint,timeout_ms); query(question)` | 保留现有拼写的早期字符串 RAG 客户端，返回原始响应；新代码优先使用统一请求器/流式接口。 |
| [rag_response_parser.h](../include/rag_response_parser.h) | `RagResponseParser::extractAnswerOrRaw(response)` | 从旧 Python RAG JSON 提取 generated_answer，错误文本和非 JSON 有兼容处理；不是严格协议校验器。 |
| [rag_stream_client.h](../include/rag_stream_client.h) | `RagStreamClient(endpoint,timeout_ms); name/query/endpoint/timeoutMilliseconds` | DEALER 客户端发送 RAG 流式 JSON，校验 request_id/sequence、积累回答、传递事件，默认等待 120000ms。 |
| [rag_control_client.h](../include/rag_control_client.h) | `RagControlClient(requester,endpoint,timeout_ms); name/cancel/endpoint/timeoutMilliseconds` | 独立控制通道发送 cancel 并校验回应，默认 2000ms。 |
| [rag_stream_protocol.h](../include/rag_stream_protocol.h) | `RagStreamProtocol::encodeRequest/decodeRequest/encodeEvent/decodeEvent/eventTypeToString; KVersion` | RAG v1 请求与 chunk/finished/error 严格编解码；非法字段或事件抛异常。 |
| [rag_control_protocol.h](../include/rag_control_protocol.h) | `RagControlProtocol::encodeRequest/decodeRequest/encodeResponse/decodeResponse; KVersion` | v1 cancel/cancel_result 编解码，校验空 ID 和 ok/cancelled/error 一致性。 |
| [llm_protocol.h](../include/llm_protocol.h) | `LlmProtocol::encodeRequest/decodeRequest/encodeResponse/decodeResponse/encodeStreamEvent/decodeStreamEvent` | LLM v1 generate/generation_result 与流式事件；与 RAG 的 query/事件类型不同。 |
| [llm_service.h](../include/llm_service.h) | `LlmService(backend); handleMessage(message) → string` | 非流式服务逻辑：解析/校验请求，调用 generate，统计耗时并返回编码 JSON；传输 socket 在应用入口。 |
| [llm_stream_service.h](../include/llm_stream_service.h) | `LlmStreamService(backend); handleMessage(message,emitter); cancel(id); activeRequestId()` | 将 generateStream 转为编码事件；维护活动 ID，使控制线程能定位取消；不是任意多请求调度器。 |
| [llm_control_server.h](../include/llm_control_server.h) | `LlmControlServer(stream_service,endpoint); start/stop/running` | 独立 REP 工作线程处理取消消息；析构停止并回收线程，服务对象须先于它存活。 |

## Agent 与设备

| 头文件 | 公开入口 | 功能与约定 |
| --- | --- | --- |
| [agent/agent_types.h](../include/agent/agent_types.h) | `AgentToolCall/AgentAction/AgentToolResult/AgentObservation/AgentPlanningContext/AgentResponse` | 工具名称和 arguments、动作类型、工具结果、历史和 Completed/WaitingForConfirmation/Failed；静态工厂构造成功/失败对象。 |
| [agent/agent_planner.h](../include/agent/agent_planner.h) | `AgentPlanner::name/plan(context)/cancel` | 规划下一步 FinalAnswer/ToolCall/Error；cancel 默认 false；不执行设备操作。 |
| [agent/rule_agent_planner.h](../include/agent/rule_agent_planner.h) | `RuleAgentPlanner::name/plan` | 确定性规则和 Observation 回答，用于无模型基线；不等同于开放式多条件理解。 |
| [agent/llm_agent_planner.h](../include/agent/llm_agent_planner.h) | `LlmAgentPlanner(llm,registry,config); name/plan/parseResponse/buildPrompt/cancel` | 以工具定义与历史构建 prompt，解析 JSON、校验工具；maximum_response_bytes 默认 8192；parseResponse/buildPrompt 也供测试。 |
| [agent/agent_tool.h](../include/agent/agent_tool.h) | `AgentTool::name/description/parametersSchema/requiresConfirmation/execute(arguments)` | schema 声明参数，具体 execute 实现验证；requiresConfirmation 默认 false，写工具重载为 true。 |
| [agent/tool_registry.h](../include/agent/tool_registry.h) | `ToolRegistry::registerTool(unique_ptr); find(name); execute(call); definitions()` | 注册并拥有工具，find 返回非拥有指针；execute 通过白名单分发并返回 AgentToolResult；definitions 提供模型可见 schema。 |
| [agent/agent_executor.h](../include/agent/agent_executor.h) | `AgentExecutor(planner,registry,config); run(session_id,user_input); hasPendingAction/clearSession/cancel` | 控制多步循环与每会话待确认动作，默认4步/30秒；注入 now 便于超时测试；取消不回滚已执行工具。 |
| [agent/agent_intent_classifier.h](../include/agent/agent_intent_classifier.h) | `AgentIntentClassifier::classify(query); intentToString` | 规则区分环境查询、设备状态、控制和条件工作流；None 回退普通问答，含 confidence/reason。 |
| [agent/agent_answer_backend.h](../include/agent/agent_answer_backend.h) | `AgentAnswerBackend(executor,config); name/query/cancel/hasPendingAction` | 把 AgentResponse 包装为流式回答接口并记录执行结果；默认固定 session_id 保存跨轮确认。 |
| [agent/agent_routing_answer_backend.h](../include/agent/agent_routing_answer_backend.h) | `AgentRoutingAnswerBackend(agent,fallback,fallback_cancel); name/query/cancel` | 安全分类、pending 和意图分发；追踪活动后端，让取消传到正确目标。 |
| [agent/vehicle_device.h](../include/agent/vehicle_device.h) | `VehicleDevice::readEnvironment/setAirConditionerEnabled/airConditionerEnabled; MockVehicleDevice::setEnvironment/setControlFailure` | 环境为 temperature_c/humidity_percent；Mock 注入环境和控制失败，控制返回 bool。 |
| [agent/linux_vehicle_device.h](../include/agent/linux_vehicle_device.h) | `LinuxVehicleDevice(iio_root,name); readEnvironment/setAirConditionerEnabled/airConditionerEnabled` | 按 name 搜索 iio:device*，读毫单位温湿度并换算、检查范围，读取错误抛异常；空调状态仅 atomic_bool。 |
| [agent/vehicle_tools.h](../include/agent/vehicle_tools.h) | `GetCabinEnvironmentTool/CheckCabinTemperatureConditionTool/GetAirConditionerStateTool/SetAirConditionerIfTemperatureTool/SetAirConditionerTool` | 五类工具共用 name/description/parametersSchema/execute；后两类要求确认；参数、结果和错误见 protocols.md。 |

## 关键结果结构

| 结构 | 应读取的字段 | 解释 |
| --- | --- | --- |
| `RagQueryTiming` | retrieval_elapsed_ms、llm_time_to_first_token_ms、llm_elapsed_ms、first_token_observed | 未观测首 token 时不能把 0ms 解释为零延迟 |
| `VoiceAssistantResult` | ok/error、query/answer、response_mode/reason、rag_timing、timing、chunk/sentence计数 | 远程后端未必提供本地细分耗时 |
| `VoiceAssistantInterruptResult` | had_active_request、output_stopped、cancel_attempted/cancel_ok/generation_cancelled | 分别表示有活动请求、本地停播、尝试及真正取消生成 |
| `AgentResponse` | state、answer/error、executed_tool、observation、trace | WaitingForConfirmation 时 ok 可为 true，但工具尚未执行 |
| `VoiceQueueTiming` / `VoiceAssistantTiming` | capacity、peak_depth、push_wait_count/ms、queue_metrics_observed/complete | 只采数据push的容量等待；深度为共享队列总占用；取消/失败快照可能不完整，见[性能指南](performance-guide.md) |
| `BgeEmbeddingTiming` / `DenseRetrievalTiming` | observed、token_count、lock/prepare/inference/postprocess/index/mapping/total | 显式Profiled路径的应用层墙钟分段，不是算子trace或硬件占比 |
| `VoicePerformanceStatistics` | valid/invalid/ignored计数、successful_report_count、reported_success_rate、metrics | 成功率只在日志内有效 PERF_JSON 报告上计算，不能作为所有请求的成功率 |

通信 JSON、工具 schema 和索引数据见[协议文档](protocols.md)。Python 各模块入口及类/函数索引见[源码文件索引](source-map.md)。

RuleAgentPlanner对EnvironmentQuery复用AgentIntentClassifier判定，覆盖“现在多少度”等口语查询，继续通过get_cabin_environment执行IIO/Mock读取；工具接口、参数与确认机制未变。板端PCM选择和私有配置加载见[采样率冲突修复](rk3588-duplex-fix.md)。


## 板端TTS配置映射

[board_tts_config.h](../include/board_tts_config.h)新增
makeBoardTtsConfig(const BoardAppConfig&) → SherpaOnnxTtsConfig，返回独立值配置，不创建SDK资源。
主程序、输出检查与TTS基准共用，保证模型文件、词表、date/number FST、说话人和线程一致。

BoardAppConfig新增tts_model_file（目录内文件名，旧conf缺省model.int8.onnx）、
tts_speaker_id（缺省0，非负，实际模型创建时检查上界）。
当前板端conf显式选model.onnx / speaker0；CPU性能与内存对照见[TTS优化](tts-optimization.md)。


## 连续会话打断开关

ContinuousVoiceSessionConfig新增末尾字段barge_in_enabled=true，保留原max_turns/future_poll_interval_ms的聚合初始化顺序。
BoardAppConfig同名字段旧conf缺省true，false关闭回答期间的打断录音；主程序以同一标志控制第二套VAD创建和会话模式。
关闭时未使用barge recorder的录音/取消/stop接口，主动stop仍停止正常录音与助手并等待相关工作回收。
开启时行为及request/session语义保持原样。当前板端参考conf显式false，详见[主线验收](board-mainline-acceptance.md)。
