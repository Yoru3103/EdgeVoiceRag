# C++测试手册：板端主线、并发与SDK集成

更新：2026-11-12。当前重点为RK3588板端C++主线；PC/WSL执行Host与SDK测试。Python RAG/语音和训练测试不在本轮范围内。

## 1. 测试范围与数量

当前默认注册 **43个CTest条目**，对应42个测试可执行程序；`voice_lifecycle_stress` 复用 `voice_lifecycle_tests --stress`。ALSA、sherpa ASR/TTS/VAD、BGE全部开启时额外注册9项，共 **52项**。一个CTest条目通常包含多个断言/用例，不能把条目数当成语音轮数。

| 层次 | 检查目标 | 当前入口 |
| --- | --- | --- |
| 基础行为 | 数据、队列、分句、检索、工具和错误处理 | 各模块测试 |
| 生命周期 | 精确控制合成/播放/队列/取消的先后顺序 | 18类VoiceAssistant场景 + 16类ContinuousVoiceSession场景 |
| 混合压力 | 重建对象与长期复用时的取消、异常恢复和音频隔离 | 1000场景 + 同一助手1000请求 |
| 运行时诊断 | 执行路径中的非法内存访问和未定义行为 | 独立ASan/UBSan构建 |
| SDK及输出集成 | 模型资产、推理适配、索引与独立输出入口 | 8项后端/模型测试 + 1项输出冒烟 |

默认`board_mainline` 标签筛出18项（ALSA开启额外增加1项输出冒烟），其中两项为 `lifecycle`，一项为 `stress`。其余检索/基础/通信回归仍在全量CTest中执行。保留的C++通信回归使用注入传输或本地测试服务，无需额外启动Python服务。

2026-11-08历史执行：普通Debug **43/43**，ASan/UBSan **43/43**，当时PC可选SDK **51/51**（当时尚无输出冒烟条目）。详细环境、修复前失败证据及日志见[2026-11-08记录](experiments/2026-11-08-voice-lifecycle.md)。

## 2. 每个CTest条目做什么

### 2.1 语音、队列与生命周期

| CTest名称 / 源码 | 测试功能 | 输入与依赖 |
| --- | --- | --- |
| [bounded_blocking_queue_tests](../tests/bounded_blocking_queue_tests.cpp) | FIFO、满/空等待、close唤醒与排空、clear清空并唤醒生产者、移动语义、零容量拒绝及按次容量等待/深度观测。 | 纯Host |
| [streaming_sentence_buffer_tests](../tests/streaming_sentence_buffer_tests.cpp) | 跨Chunk分句、多句/尾句、UTF-8标点分片、清理残留、自定义分隔符与非法配置。 | 纯Host |
| [voice_assistant_tests](../tests/voice_assistant_tests.cpp) | 流式及非流式回答、TTS/播放线程、阶段失败、输入校验、取消生成、无活动请求打断、重复停止与禁止重启。 | Mock后端 |
| [voice_input_pipeline_tests](../tests/voice_input_pipeline_tests.cpp) | 录音→ASR→回答完整单轮、无效PCM/空文本/各阶段失败短路、启动要求及停止录音。 | Mock音频/ASR |
| [continuous_voice_session_tests](../tests/continuous_voice_session_tests.cpp) | 正常连续会话的AnswerCompleted事件，录音、ASR、检索、LLM和首播计时能否保留。 | Mock；主要为正常路径 |
| [voice_lifecycle_tests](../tests/voice_lifecycle_tests.cpp) | 18类受控合成/播放打断、两级队列背压、停止、后端失败/异常与下一请求恢复。 | 详见第3节 |
| [voice_lifecycle_stress](../tests/voice_lifecycle_tests.cpp) | 同一程序的--stress模式：1000个混合场景，另加同一助手1000轮唯一ID请求。 | 固定种子20261108；120秒 |
| [continuous_voice_session_lifecycle_tests](../tests/continuous_voice_session_lifecycle_tests.cpp) | 12类录音衔接、取消回退、失败短路、会话停止与参数检查。 | 详见第4节 |
| [wav_audio_reader_tests](../tests/wav_audio_reader_tests.cpp) | PCM16单/双声道WAV读取、错误头/位深/缺失文件、WAV录音适配及停止后拒绝。 | 临时WAV文件 |

### 2.2 Agent与设备

| CTest名称 / 源码 | 测试功能 | 输入与依赖 |
| --- | --- | --- |
| [agent_executor_tests](../tests/agent_executor_tests.cpp) | 只读直接执行、写操作确认、确认/取消、会话隔离、非法/未知/重复工具、设备失败、确认过期。 | Rule Planner + Mock |
| [llm_agent_planner_tests](../tests/llm_agent_planner_tests.cpp) | 工具/最终答复JSON解析、Markdown包裹、条件工具、非法JSON/工具/参数、LLM失败。 | 预设LLM输出 |
| [agent_workflow_tests](../tests/agent_workflow_tests.cpp) | 查询→条件判断→控制→Observation规划闭环、条件不满足、原子条件工具和最大步数。 | 预设规划序列 |
| [agent_answer_backend_tests](../tests/agent_answer_backend_tests.cpp) | Agent业务结果封装为流事件、跨request_id确认、取消清Pending、空请求拒绝。 | 固定session_id |
| [agent_routing_answer_backend_tests](../tests/agent_routing_answer_backend_tests.cpp) | 环境/控制进入Agent、确认继续、手册/闲聊回退、安全问题覆盖Pending。 | Mock fallback |
| [linux_vehicle_device_tests](../tests/linux_vehicle_device_tests.cpp) | 按IIO name定位、设备号变化、毫单位换算、缺失设备报错及内存空调状态。 | 临时sysfs；无真实IIO |
| [vehicle_tools_tests](../tests/vehicle_tools_tests.cpp) | 工具Schema、温度比较边界、参数与传感器校验、确认后重读条件、控制失败/未命中不写。 | Mock设备 |

### 2.3 检索与索引契约

| CTest名称 / 源码 | 测试功能 | 输入与依赖 |
| --- | --- | --- |
| [chinese_text_tokenizer_tests](../tests/chinese_text_tokenizer_tests.cpp) | 中文unigram/bigram、标点边界、ASCII归一化、混合文本、重复词、空/非法UTF-8。 | 纯Host |
| [knowledge_base_loader_tests](../tests/knowledge_base_loader_tests.cpp) | chunks加载、aliases与retrieval_text契约及事实text保留。 | 本地chunks.json |
| [bm25_retriever_tests](../tests/bm25_retriever_tests.cpp) | 词项/索引统计、典型车辆问题、同义召回、Top-K、空查询和非法配置/缺失资产。 | 本地chunks.json |
| [dense_vector_index_tests](../tests/dense_vector_index_tests.cpp) | 向量加载、归一化查询、精确排序/并列、Top-K、维数/二进制大小/重复ID/非归一化错误。 | 临时向量资产 |
| [hybrid_retriever_tests](../tests/hybrid_retriever_tests.cpp) | 加权RRF融合、单路回退、候选深度、并列稳定性与配置校验。 | Mock稀疏/稠密检索 |
| [relevance_filtering_retriever_tests](../tests/relevance_filtering_retriever_tests.cpp) | 原始稀疏/稠密阈值、弱结果拒绝、后续候选保留、原分数与候选深度。 | Mock检索 |
| [retriever_runtime_tests](../tests/retriever_runtime_tests.cpp) | 按配置装配BM25/过滤/Dense/Hybrid、未加载访问、未知或未编入后端。 | chunks；SDK开启时增加真实模型分支 |
| [file_fingerprint_tests](../tests/file_fingerprint_tests.cpp) | 已知FNV-1a指纹、内容变化与缺失文件错误。 | 临时文件 |

### 2.4 生成、策略、配置与观测

| CTest名称 / 源码 | 测试功能 | 输入与依赖 |
| --- | --- | --- |
| [edge_voice_rag_unit_tests](../tests/unit_tests.cpp) | 早期QueryRouter、RagEngine、文本RAG结果解析与查询分类的基础断言。 | 源码为unit_tests.cpp |
| [llm_backend_factory_tests](../tests/llm_backend_factory_tests.cpp) | Mock创建、未知后端及未编入RKLLM时的拒绝。 | 不运行RKLLM |
| [llm_backend_stream_tests](../tests/llm_backend_stream_tests.cpp) | Mock流式Chunk、空Prompt/空回调拒绝、基类非原生流式兼容。 | Mock生成 |
| [local_rag_llm_backend_tests](../tests/local_rag_llm_backend_tests.cpp) | 本地检索与LLM拼接、请求校验、取消接口。 | 本地chunks + Mock LLM |
| [policy_routing_answer_backend_tests](../tests/policy_routing_answer_backend_tests.cpp) | Safety/DirectRag/RagLlm/LlmOnly/Clarification、创作跳过检索、阈值及当前知识库路由。 | 本地chunks + Mock LLM |
| [board_app_config_tests](../tests/board_app_config_tests.cpp) | board_wsl.conf中的Hybrid、过滤阈值、候选深度与直答阈值契约。 | 只解析配置；不装载语音模型 |
| [voice_performance_report_tests](../tests/voice_performance_report_tests.cpp) | 完成事件/可选队列扩展编码、未观测标记、错误事件类型和request_id拒绝。 | 构造事件 |
| [voice_performance_statistics_tests](../tests/voice_performance_statistics_tests.cpp) | 有效/无效/忽略样本、阶段观测；旧/新队列字段兼容，完整0ms保留、部分快照排除、畸形组拒绝。 | 构造PERF_JSON |

### 2.5 既有文本与通信回归

| CTest名称 / 源码 | 测试功能 | 输入与依赖 |
| --- | --- | --- |
| [multi_level_response_tests](../tests/multi_level_response_tests.cpp) | 旧策略分类、RAG/LLM选择、回退、缓存命中、失败不缓存与空查询。 | 旧文本路线 |
| [edge_response_backend_tests](../tests/edge_response_backend_tests.cpp) | local/ZeroMQ RAG与LLM请求适配、Requester失败。 | 注入Requester |
| [streaming_answer_backend_tests](../tests/streaming_answer_backend_tests.cpp) | 流式抽象调用、后端名、结果工厂与客户端接口实现。 | Mock |
| [llm_protocol_tests](../tests/llm_protocol_tests.cpp) | 普通LLM请求/响应往返、版本与空Prompt校验。 | JSON契约 |
| [llm_service_tests](../tests/llm_service_tests.cpp) | 成功生成、后端失败、流标记拒绝、非法请求和长UTF-8 Prompt。 | Mock LLM |
| [llm_stream_service_tests](../tests/llm_stream_service_tests.cpp) | 逐段事件、终态序号、Chunk后失败、非流请求拒绝及取消活动请求。 | Mock LLM |
| [llm_control_server_tests](../tests/llm_control_server_tests.cpp) | 空闲/非法/活动请求取消，空闲超时后的服务存活。 | C++本地通信 |
| [rag_stream_protocol_tests](../tests/rag_stream_protocol_tests.cpp) | RAG事件编解码、版本/sequence/Chunk状态/请求文本校验。 | JSON契约 |
| [rag_stream_client_tests](../tests/rag_stream_client_tests.cpp) | 成功流、序号错误、服务端错误及非法配置。 | 受控通信 |
| [rag_control_protocol_tests](../tests/rag_control_protocol_tests.cpp) | 取消请求/响应、inactive与error状态、字段类型和ID校验。 | JSON契约 |
| [rag_control_client_tests](../tests/rag_control_client_tests.cpp) | 成功/无活动取消、传输失败、错误ID/消息、空请求及非法配置。 | 受控传输 |

### 2.6 可选SDK集成（额外8项）

| CTest名称 / 源码 | 测试功能 | 输入与依赖 |
| --- | --- | --- |
| [sherpa_onnx_asr_backend_tests](../tests/sherpa_onnx_asr_backend_tests.cpp) | ASR线程数、语言及缺失模型配置拒绝；不评测真实语音转写准确率。 | EDGE_ENABLE_SHERPA_ASR |
| [sherpa_onnx_tts_backend_tests](../tests/sherpa_onnx_tts_backend_tests.cpp) | 线程数/缺失模型/空文本拒绝及真实TTS合成。 | SHERPA_TTS + TTS模型 |
| [alsa_audio_backend_tests](../tests/alsa_audio_backend_tests.cpp) | 无效录放配置、空播放及错误上下文；同一null播放器16k单声道→44.1k单声道→48k双声道→stop→重新播放；null采集。 | EDGE_ENABLE_ALSA；无真实声卡 |
| audio_output_check_smoke | 实际CLI以44.1k单声道tone向null设备重复播放2次，覆盖应用装配；15秒超时。 | EDGE_ENABLE_ALSA；board_mainline/audio_output标签，无模型或真实声卡 |
| [sherpa_onnx_vad_audio_recorder_tests](../tests/sherpa_onnx_vad_audio_recorder_tests.cpp) | 读取air_conditioner.wav，经内存PCM流检测语音，检查静音超时和非16kHz拒绝。 | SHERPA_VAD + Silero模型 + 本地WAV |
| [bge_embedder_tests](../tests/bge_embedder_tests.cpp) | 真实编码、维度/归一化、索引检索；Profiled与普通向量一致，错误timing不伪造观测。 | BGE模型/tokenizer/索引 |
| [dense_retriever_tests](../tests/dense_retriever_tests.cpp) | 真实Dense语义/Top-K/非法查询；Profiled排序/分数一致，非法查询清空上次timing。 | BGE + chunks + 索引 |
| [hybrid_retriever_integration_tests](../tests/hybrid_retriever_integration_tests.cpp) | 真实BM25+Dense融合召回、原始两路分数及RRF分数保留。 | BGE + chunks + 索引 |
| [dense_index_consistency_tests](../tests/dense_index_consistency_tests.cpp) | 有效索引通过，修改chunks导致指纹过期时拒绝。 | BGE + 临时chunks副本 |

## 3. VoiceAssistant受控生命周期场景

源码：[voice_lifecycle_tests.cpp](../tests/voice_lifecycle_tests.cpp)，测试辅助：[voice_test_support.h](../tests/support/voice_test_support.h)。

测试PCM只有一个样本，旧请求用101、新请求用202标记。这样断言检查的是“哪一轮音频到达播放器”，无需真实声音或主观听辨。句子和音频队列容量均设为1，以小输入制造背压。

`Event` 用mutex/CV设置与等待检查点，最长等待5秒；超时表示测试失败，不用于决定线程先后。Fixture清理时先释放所有Mock暂停点，再stop/join并回收future，避免断言失败导致测试自身挂住。18类场景如下，表中成对名称分别代表失败返回和异常两条独立用例：

| 场景名（与控制台输出一致） | 通过条件 |
| --- | --- |
| `normal_completion` | 正常请求完整合成、播放并返回；PCM标记为202。 |
| `destructor_reclaims_idle_workers` | 不显式stop而销毁助手，析构唤醒/回收空闲工作线程，同一组后端可再次使用。 |
| `cancel_during_tts_and_start_next` | 旧TTS保持暂停，先取消并进入新请求，再放开旧TTS；播放器只能收到新PCM，取消ID只能是旧请求。 |
| `cancel_during_playback` | 旧PCM已开始播放时打断；允许已发生的播放调用，旧播放不记成功，新请求完整播放。 |
| `cancel_with_full_sentence_queue` | TTS持有第1句、容量1的句子队列持有第2句、第3句准备入队；取消后旧句不进入播放，新请求可继续。 |
| `cancel_with_full_audio_queue` | 播放器持有第1段、容量1音频队列持有第2段、TTS产生第3段；取消后排队旧PCM不播放。 |
| `cancel_blocked_generation` | 生成等待取消信号，检查取消目标和返回状态，之后新请求成功。 |
| `stop_during_tts` | stop进入本地停止后才释放TTS，检查退出、线程join、重复stop及禁止重启。 |
| `stop_with_full_sentence_queue` | 满句子队列下停止，关闭/清空能使请求退出，TTS线程被回收。 |
| `stop_with_full_audio_queue` | 满音频队列下停止，播放器、合成和请求均能退出。 |
| `answer_failure_then_recover / answer_exception_then_recover` | 回答后端返回失败/抛异常时报告错误，随后新请求成功。 |
| `tts_failure_then_recover / tts_exception_then_recover` | 合成返回失败/抛异常时不播出旧PCM，随后新请求成功。 |
| `playback_failure_then_recover / playback_exception_then_recover` | 播放返回失败/抛异常时本轮失败，随后新请求成功。 |
| `cancel_failure_keeps_local_output_stopped / cancel_exception_keeps_local_output_stopped` | 远端取消失败/抛异常仍保留本地停止效果，并报告取消错误；旧TTS结果不能播出。 |

“已播放的声音无法撤销”与“旧排队PCM不得在下一轮继续播放”分别断言；测试不把取消当作回滚。异常恢复用例还要求下一请求成功，避免只确认报错而漏掉流水线已失效的情况。

## 4. ContinuousVoiceSession受控会话场景

源码：[continuous_voice_session_lifecycle_tests.cpp](../tests/continuous_voice_session_lifecycle_tests.cpp)。

普通录音返回PCM101，打断录音返回PCM202。ASR把样本标记转成查询文本；若丢失打断录音而重新普通录音，Mock会直接报错。三种录音衔接场景还检查：只做一次普通录音、ASR输入按101/202排列、两轮request_id不同、录音耗时34ms得以保留、中断计数一次、结束后工作线程已停止。

| 场景名 | 通过条件 |
| --- | --- |
| `completed_capture_survives_answer_first` | 录音成功结果延迟到cancel唤醒后才发布，强制进入answer先完成分支；成功录音必须保留。 |
| `speech_callback_preserves_next_capture` | 已起声并调用stopPlayback后，完整录音进入下一轮，不重复普通录音。 |
| `capture_without_callback_uses_fallback_cancel` | 未触发起声回调但已得到完整录音时，主会话回退取消旧回答并复用录音。 |
| `recording_failure_stops_downstream` | 录音失败时不调用ASR/回答，工作线程回收。 |
| `asr_failure_stops_downstream` | ASR失败不进入回答，返回错误并回收线程。 |
| `empty_transcription_is_rejected` | 空转写被拒绝，不进入回答。 |
| `answer_failure_reclaims_workers` | 回答失败向会话传播，并取消监听、回收工作线程。 |
| `stop_during_recording` | 阻塞普通录音时stop，唤醒录音并退出，不进入ASR。 |
| `stop_during_answer` | 生成和打断监听活动时stop，两个future和常驻线程最终退出。 |
| `stop_during_asr` | ASR暂停时stop，ASR返回后不再启动回答任务。 |
| `stop_from_recognized_callback` | RecognizedText回调同步stop后，不再派发回答任务。 |
| `reject_zero_poll_interval` | 轮询间隔为0时构造失败。 |

第一项通过“cancel后才发布成功录音”强制选择回答先完成分支，不依赖sleep或哪条线程碰巧更快。它与第二项在修复前均能暴露下一轮录音丢失；修复保留成功capture，已观察到起声时等待该语句录完；停止边界另在ASR返回和识别事件回调后检查stop_requested，防止上述停止路径派发新任务。

`completed_turns` 沿现有语义计入已中断的旧轮；这三个衔接场景设置 `max_turns=2`，允许下一轮被处理。测试没有把它重新定义为成功播报数量。

## 5. 固定种子压力测试的两个阶段

`voice_lifecycle_tests --stress` 固定使用种子 **20261108**：

1. **1000个生命周期场景**：前18次保证每类场景至少一次，之后按mt19937结果取模选择。每个场景独立装配助手，覆盖构造、取消/失败恢复及销毁；一个场景可能发起一或两个请求，不等于1000轮持续会话。
2. **同一助手连续1000个请求**：每轮使用唯一 `stress-request-N`。本次顺序为673轮正常、327轮取消。取消轮发送未成句旧文本并阻塞生成；取消后新请求必须没有残留文本，播放器成功PCM数量逐轮等于正常请求数，取消记录必须与目标ID列表完全相等。

两个阶段均用检查点同步，不引入随机sleep。每轮future/Mock等待有5秒界限，整个压力条目有120秒进程超时。失败输出包含阶段、场景或请求ID及固定种子，便于复现；通过输出含各场景次数和正常/取消数量。

这验证当前Mock模型下的行为与生命周期，不穷尽所有线程调度，也没有证明真实RKLLM SDK回调可并发或可以在query返回后继续回调。数据竞争检查仍需独立TSan，真实声卡、IIO硬件、RKLLM/NPU与板端时延另行验收。

## 6. 如何运行

所有命令在Linux/WSL项目根目录执行；普通Host与诊断构建的准备方法见[诊断入门](diagnostics.md)。

```bash
# 默认全部43项；先构建全部目标，避免只生成部分测试程序
cmake -S . -B build-host-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-host-debug -j4
ctest --test-dir build-host-debug --output-on-failure --timeout 30

# 只看两组新生命周期场景
ctest --test-dir build-host-debug -L lifecycle -V

# 查看固定种子、场景计数和同一助手1000轮结果
ctest --test-dir build-host-debug -L stress -V

# 快速主线检查（排除压力条目，共17项）
ctest --test-dir build-host-debug -L board_mainline -LE stress --output-on-failure

# 生命周期场景分别重复20次，观察多种调度
ctest --test-dir build-host-debug -L lifecycle \
  --repeat until-fail:20 --output-on-failure
```

ASan/UBSan版本：

```bash
cmake -S . -B build-host-asan-ubsan \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DEDGE_ENABLE_ASAN=ON -DEDGE_ENABLE_UBSAN=ON
cmake --build build-host-asan-ubsan -j4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-host-asan-ubsan --output-on-failure --timeout 30
```

可选SDK配置见[开发文档](development.md)。启用全部五个Host可用后端后，在对应独立目录执行全量CTest；RKLLM在本次PC配置中关闭。ASan/UBSan只对Host Mock组合执行，SDK组合本次使用普通Release。

## 7. 资产、日志与维护

- 默认部分测试依赖本地chunks；可选集成依赖BGE模型/索引、TTS模型、Silero VAD和 `voice_input/air_conditioner.wav`。这些资产多数被Git忽略，新克隆需按开发文档准备，不能用空文件替代。
- CTest完整输出在构建目录的 `Testing/Temporary/LastTest.log`，再次执行可能覆盖；保存需要的失败日志和源码/资产指纹。
- 新增测试源码在 `tests/CMakeLists.txt` 显式注册。新增场景若需暂停工作线程，暂停点必须能在失败清理时释放，控制器不要持有被测代码还需获取的锁。
- 修改公共行为同步[项目流程图](project-flow.md)、接口说明和测试通过条件。新增测试后用 `ctest --show-only=json-v1` 检查注册数量、参数、标签和超时。
- 判断通过需同时看业务断言、进程退出码、超时和Sanitizer报告；重复通过不能替代具体危险顺序的行为断言。


## 8. 离线性能记录器回归

`tests/test_retrieval_baseline.py` 有13项标准库测试：Release/Sanitizer门禁、run/case完整唯一性、分段与真实0统计、非法时延/缺失汇总、原生命令资源与退出码、已有目录保持不变、错误输出仍保存failed清单。它不启动Python服务，也不访问训练环境。

```bash
python3 -m unittest discover -s tests -p test_retrieval_baseline.py -v
```

该检查不增加CTest条目数量。新性能观测的实测与口径见[性能指南](performance-guide.md)。

补充覆盖BusyBox time识别/未知实现拒绝、无Git/本机编译器的交叉清单成功路径、二进制hash不符时不启动子进程、AArch64/Host/截断ELF校验、打包输出不覆盖。它们是本地标准库测试，不操作声卡或传感器。板端测试方法见[手动性能指南](rk3588-performance.md)。

## DHT11内核模块的Host帧解析验证

独立于用户态CTest，见[模块测试命令](../kernel_modules/edge_dht11/README.md)。共享解析器有10组语义场景，普通构建与ASan/UBSan均可运行；覆盖完整82/83沿、截断与失败不发布结果、坏校验、电平/时间戳/数值范围、非法参数及真实成功帧回放。它们验证解析逻辑，不验证GPIO/IRQ电气行为。

DHT11补充[GPIO/IRQ流程回归](../kernel_modules/edge_dht11/tests/test_acquisition.py)：2项unittest分别执行普通/ASan+UBSan构建，各运行9类实际采集函数的模拟场景，覆盖注册IRQ后拉低、重复测量、申请/拉低失败、超时/中断及资源与缓存清理。板端行为仍需实测，见[0.2.1修订记录](experiments/2026-11-09-dht11-od-value.md)。

DHT11延迟对照工具的标准库测试：[test_dht11_latency_probe.py](../tests/test_dht11_latency_probe.py)，用 `python3 -m unittest discover -s tests -p test_dht11_latency_probe.py -v` 单独执行。7项测试模拟QoS请求与IIO读取，覆盖设备选择、关闭/中断/短写清理、失败计数和A/B顺序，不做板端采样。操作见[失败边沿与延迟对照](experiments/2026-11-09-dht11-failed-edges.md)。

DHT11 0.2.2追加QoS生命周期、坏校验与缓存命中/过期场景：2项采集测试分别执行普通/Sanitizer版本，每版9类，检查请求不泄漏及缓存命中不申请。板端外部QoS对照结果为0/5与5/5；新版模块尚待手动验证，见[窗口QoS记录](experiments/2026-11-09-dht11-qos-window.md)。

## Linux IIO → Rule Agent联合验证

linux_vehicle_device_tests保留原有4组设备读取/状态用例，增加3组跨模块场景：按name选取动态编号设备并换算到工具Observation，后续读取值更新；写动作等待确认、取消不改变状态、确认后状态可查询；非法IIO整数及缺失设备的Agent错误传播和恢复。

测试使用临时sysfs样式目录，不能证明真实DHT11或物理空调控制。本次默认43项CTest全通过，独立ASan/UBSan该目标1项通过；CTest数量未增加。Agent入口参数及板端手动验收见[主线联调步骤](board-agent-bringup.md)。

## 2026-11-10独立语音输出增量

默认无SDK配置仍为43项且本次全部通过；ALSA-only配置增加后端和输出冒烟，45/45通过。已有PC可选SDK目录聚焦这两项2/2通过，另实际加载现有TTS模型生成WAV并重复提交null设备2次；未重新全跑52项，不能把历史51项套成本次全量结果。

本次额外实跑13项CLI/文件检查：16k/44.1k/48k三种tone的PCM16单声道WAV与重复播放、已有WAV拒绝覆盖、6类非法参数、TTS未编入错误、设备打开失败时保留已导出WAV、真实TTS及WAV头/帧数验证。它们的原始日志在.cache/audio-output-checks-20261110-vfb9u5ii，不是新增13项CTest。

独立Release ARM配置仅开ALSA/TTS，BUILD_TESTING=OFF，audio_output_check交叉编译成功；包内ELF、依赖和校验表通过本地检查。板端声音与原ALSA故障原因仍需使用者[手动验证](board-audio-output.md)，本轮未操作板端。详见[实验记录](experiments/2026-11-10-audio-output-check.md)。

## 采样率对称冲突与口语环境查询回归

本轮默认43/43、ALSA-only45/45通过（条目数未增加）。alsa_audio_backend_tests加载真实项目ALSA配置，仅将物理设备替换为asym file/null，保持16k采集handle存活，再播放1秒44.1k非零测试音；检查源格式不变、文件端约16000帧而非44100帧。允许重采样尾部的小量延迟，未将其当成真实声卡对称约束模拟器。

agent_routing_answer_backend_tests增加“现在多少度”“车里多少度”“热不热”等完整Router→Rule→工具读数断言与环境值更新；linux_vehicle_device_tests以“现在多少度”检查后续IIO值刷新。均为语义行为断言，不仅检查route标签。真实Host TTS转换也通过；[板端验收](rk3588-duplex-fix.md)待使用者执行。


## 2026-11-11 TTS配置与性能增量

本轮默认CTest43/43、ALSA-only45/45、可选SDK聚焦4/4通过：配置、ALSA、输出冒烟和真实TTS。
board_app_config_tests增加旧conf缺省兼容、FP32文件/speaker66/线程映射及6类非法选择；
alsa_audio_backend_tests将固定16k硬件端file/null回归扩展为8k与44.1k两种源音频。
主程序、输出探针和新增基准的Host/ARM目标均构建成功，ARM仍BUILD_TESTING=OFF。
入口归src/apps后的默认构建/43项及Host/ARM目标路径也通过验证。

独立基准额外8项CLI/数据错误检查通过；12组模型/线程初扫、最终3组各1次预热+5次测量及两份PCM16 WAV导出/重复null播放通过。
这些是Host实际合成，不是新增CTest条目，未重新全跑52项或重跑ASan/UBSan。
更新包本地安装副本验证配置保留和备份，实际板端延迟/音质由使用者复测。
详情见[TTS优化](tts-optimization.md)。


## 2026-11-12主线控制增量

连续会话生命周期增加4类半双工场景（当前共16类）：正常两轮、回答失败、生成中stop、播放中stop；检查barge录音从未启动、无打断事件、正常录音逐轮进行、ID隔离以及助手线程回收。开启模式沿用旧12类回归。
board_app_config_tests覆盖开关缺省兼容与严格布尔解析。本轮默认43/43、ASan+UBSan聚焦两项2/2通过，ARM主程序构建成功；不将Mock/主机结果当作板端回收验收。
[板端步骤与阶段结果](board-mainline-acceptance.md)，思考过滤未进入本轮范围。
