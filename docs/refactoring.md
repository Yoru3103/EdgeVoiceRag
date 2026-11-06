# 目录迁移与验证记录

日期：2026-11-04。目标：让源码按职责可浏览，并补齐当前项目的真实设计与技术栈演进说明。

后续已将根CMake拆为选项、依赖查找和各模块子CMake；本页的121条路径检查是首次源码迁移时的记录。当前构建结构与本轮验证见[CMake指南](cmake-structure.md)。

## 调整范围

- 63 个 .cpp 文件迁移到职责目录，原有 src/agent 中的规划/执行代码继续保留。
- 所有 main() 集中到 src/apps；Mock/Linux IIO 实现归入 src/devices。
- CMake 显式源文件路径同步更新；库名称、可执行程序名称和可选开关保留。
- include/ 中 78 个公共头文件的位置保留，已有 #include 无需修改；设备头文件继续位于 include/agent，避免本轮扩大包含路径变更。
- README、structure.md 更新，新增项目总览、接口、协议、构建、源码索引及技术栈报告。
- .gitignore 增加本次独立验证目录 build-docs-check/。

这次是目录与文档整理。没有把尚未实现的 PTQ、RKNN、MLIR 或 DMA 实验代码提前加入主链路；后续任务列于[技术栈报告](technology-roadmap.md)。

## 原有测试问题修正

重构前用全新 Debug 目录构建：40 个默认 CTest 中 39 个通过，agent_workflow_tests 的两个断言失败。

原因是测试查找带空格的 JSON 子串，例如 `"matched": true`；当前 LlmAgentPlanner 使用紧凑 history.dump()，输出为 `"matched":true`。工作流 trace 中条件判定和设备确认行为都已通过，失败来自测试对序列化空白的假设。

现在从提示词的 Observation 区段解析 JSON，再验证 observation.data.matched 的布尔类型与值。检查仍验证工具结果确实传给后续规划步骤，且不依赖 JSON 缩进。业务源码内容没有为此修改。

## 本次实际验证

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| 重构前全新默认 Debug 构建 | 编译成功；39/40 CTest通过 | 作为失败归因基线 |
| 重构后默认 Debug 构建 | 编译成功；40/40 CTest通过 | 关闭所有模型/硬件可选开关 |
| Python 回归 | 106 passed | 使用现有 edge-rag Python，PYTHONPATH=python，包含工作树中已有评测用例 |
| PC 语音应用编译 | board_voice_assistant 编译链接成功 | ALSA、sherpa ASR/TTS/VAD、BGE=ON，RKLLM=OFF；复用已有 build-board-pc SDK配置 |
| Agent Mock CLI | 环境查询、控制确认与取消均完成，退出码0 | 不访问真实设备 |
| 文本 CLI | --once 手册问题可返回，退出码0 | 本地 BM25，无生成服务依赖的示例 |
| 检索 benchmark 冒烟 | bm25 + --filter，退出码0 | 24个正例、5个负例；固定现有小用例集，只验证工具可运行 |
| 源码内容 | 63个迁移文件与迁移前 SHA256一致 | 业务实现仅移动路径 |
| CMake路径 | 121条源码/测试/工具.cpp引用全部存在；71个src实现均在CMake引用 | 包含条件编译源码 |
| 公共API覆盖 | 78/78公共头文件进入接口参考 | 文档覆盖检查 |

默认验证命令：

```bash
cmake -S . -B build-docs-check -DCMAKE_BUILD_TYPE=Debug
cmake --build build-docs-check -j4
ctest --test-dir build-docs-check --output-on-failure
PYTHONPATH=python /home/xx/miniconda3/envs/edge-rag/bin/python -m pytest python/tests -q
```

Python 绝对路径是本机现有环境，换机器时使用自己的解释器。复用 build-board-pc 的编译验证没有运行麦克风或真实传感器。

这次未验证：RK3588 交叉编译/RKLLM链接、真实板端 ASR/LLM/TTS、DHT11 接线/驱动、真实 LED、Sanitizer、PTQ/RKNN/MLIR/DMA。报告中的目标性能和建议门槛不代表本次测量值。Debug 检索冒烟的微秒数据不作为性能优化成果。

## 保留的用户工作

本次开始时已存在：

- python/agent_finetune/evaluate_planner.py 的未提交修改。
- data/agent_planner/robustness_heldout_v3.jsonl。
- python/agent_finetune/generate_robustness_heldout_v3.py。
- python/tests/test_agent_evaluate_planner.py。
- python/tests/test_agent_robustness_heldout_v3.py。

这些文件未被本次整理修改、移动或覆盖。文档索引按当前工作树记录其存在；没有重新生成训练/评测数据或启动训练。

## 后续维护方式

新增实现放入对应 src 子目录，并显式加入 CMake；新增公共 API 同步更新 api-reference，通信/数据结构改动同步更新 protocols；每次模型/量化/Runtime 实验保存独立 manifest 和原始结果。

现有 include 仍比较扁平，下一轮可在真正拆库时一起规划头文件命名空间与迁移兼容层；避免单纯移动头文件给已有调用制造额外改动。

## 文件迁移清单


| 原路径 | 新路径 |
| --- | --- |
| `src/agent/agent_demo.cpp` | [src/apps/agent_demo.cpp](../src/apps/agent_demo.cpp) |
| `src/agent/linux_vehicle_device.cpp` | [src/devices/linux_vehicle_device.cpp](../src/devices/linux_vehicle_device.cpp) |
| `src/agent/vehicle_device.cpp` | [src/devices/vehicle_device.cpp](../src/devices/vehicle_device.cpp) |
| `src/alsa_audio_player.cpp` | [src/audio/alsa_audio_player.cpp](../src/audio/alsa_audio_player.cpp) |
| `src/alsa_audio_recorder.cpp` | [src/audio/alsa_audio_recorder.cpp](../src/audio/alsa_audio_recorder.cpp) |
| `src/alsa_pcm_stream_source.cpp` | [src/audio/alsa_pcm_stream_source.cpp](../src/audio/alsa_pcm_stream_source.cpp) |
| `src/app_config.cpp` | [src/config/app_config.cpp](../src/config/app_config.cpp) |
| `src/bge_embedder.cpp` | [src/retrieval/bge_embedder.cpp](../src/retrieval/bge_embedder.cpp) |
| `src/bm25_retriever.cpp` | [src/retrieval/bm25_retriever.cpp](../src/retrieval/bm25_retriever.cpp) |
| `src/board_app_config.cpp` | [src/config/board_app_config.cpp](../src/config/board_app_config.cpp) |
| `src/board_voice_main.cpp` | [src/apps/board_voice_main.cpp](../src/apps/board_voice_main.cpp) |
| `src/cancellable_answer_backend.cpp` | [src/response/cancellable_answer_backend.cpp](../src/response/cancellable_answer_backend.cpp) |
| `src/chinese_text_tokenizer.cpp` | [src/retrieval/chinese_text_tokenizer.cpp](../src/retrieval/chinese_text_tokenizer.cpp) |
| `src/command_line_options.cpp` | [src/config/command_line_options.cpp](../src/config/command_line_options.cpp) |
| `src/continuous_voice_session.cpp` | [src/voice/continuous_voice_session.cpp](../src/voice/continuous_voice_session.cpp) |
| `src/dense_retriever.cpp` | [src/retrieval/dense_retriever.cpp](../src/retrieval/dense_retriever.cpp) |
| `src/dense_vector_index.cpp` | [src/retrieval/dense_vector_index.cpp](../src/retrieval/dense_vector_index.cpp) |
| `src/edge_response_backend.cpp` | [src/response/edge_response_backend.cpp](../src/response/edge_response_backend.cpp) |
| `src/file_fingerprint.cpp` | [src/common/file_fingerprint.cpp](../src/common/file_fingerprint.cpp) |
| `src/hybrid_retriever.cpp` | [src/retrieval/hybrid_retriever.cpp](../src/retrieval/hybrid_retriever.cpp) |
| `src/knowledge_base_loader.cpp` | [src/retrieval/knowledge_base_loader.cpp](../src/retrieval/knowledge_base_loader.cpp) |
| `src/llm_backend_factory.cpp` | [src/inference/llm_backend_factory.cpp](../src/inference/llm_backend_factory.cpp) |
| `src/llm_control_server.cpp` | [src/transport/llm_control_server.cpp](../src/transport/llm_control_server.cpp) |
| `src/llm_protocol.cpp` | [src/transport/llm_protocol.cpp](../src/transport/llm_protocol.cpp) |
| `src/llm_server.cpp` | [src/apps/llm_server.cpp](../src/apps/llm_server.cpp) |
| `src/llm_service.cpp` | [src/transport/llm_service.cpp](../src/transport/llm_service.cpp) |
| `src/llm_stream_service.cpp` | [src/transport/llm_stream_service.cpp](../src/transport/llm_stream_service.cpp) |
| `src/local_rag_llm_backend.cpp` | [src/response/local_rag_llm_backend.cpp](../src/response/local_rag_llm_backend.cpp) |
| `src/logger.cpp` | [src/common/logger.cpp](../src/common/logger.cpp) |
| `src/main.cpp` | [src/apps/main.cpp](../src/apps/main.cpp) |
| `src/mock_llm_backend.cpp` | [src/inference/mock_llm_backend.cpp](../src/inference/mock_llm_backend.cpp) |
| `src/multi_level_response_system.cpp` | [src/response/multi_level_response_system.cpp](../src/response/multi_level_response_system.cpp) |
| `src/perf_timer.cpp` | [src/common/perf_timer.cpp](../src/common/perf_timer.cpp) |
| `src/policy_routing_answer_backend.cpp` | [src/response/policy_routing_answer_backend.cpp](../src/response/policy_routing_answer_backend.cpp) |
| `src/query_classifier.cpp` | [src/response/query_classifier.cpp](../src/response/query_classifier.cpp) |
| `src/query_router.cpp` | [src/response/query_router.cpp](../src/response/query_router.cpp) |
| `src/rag_client.cpp` | [src/apps/rag_client.cpp](../src/apps/rag_client.cpp) |
| `src/rag_client_zmq.cpp` | [src/transport/rag_client_zmq.cpp](../src/transport/rag_client_zmq.cpp) |
| `src/rag_control_client.cpp` | [src/transport/rag_control_client.cpp](../src/transport/rag_control_client.cpp) |
| `src/rag_control_protocol.cpp` | [src/transport/rag_control_protocol.cpp](../src/transport/rag_control_protocol.cpp) |
| `src/rag_response_parser.cpp` | [src/transport/rag_response_parser.cpp](../src/transport/rag_response_parser.cpp) |
| `src/rag_server.cpp` | [src/apps/rag_server.cpp](../src/apps/rag_server.cpp) |
| `src/rag_stream_client.cpp` | [src/transport/rag_stream_client.cpp](../src/transport/rag_stream_client.cpp) |
| `src/rag_stream_protocol.cpp` | [src/transport/rag_stream_protocol.cpp](../src/transport/rag_stream_protocol.cpp) |
| `src/relevance_filtering_retriever.cpp` | [src/retrieval/relevance_filtering_retriever.cpp](../src/retrieval/relevance_filtering_retriever.cpp) |
| `src/response_policy.cpp` | [src/response/response_policy.cpp](../src/response/response_policy.cpp) |
| `src/retriever_runtime.cpp` | [src/retrieval/retriever_runtime.cpp](../src/retrieval/retriever_runtime.cpp) |
| `src/rkllm_backend.cpp` | [src/inference/rkllm_backend.cpp](../src/inference/rkllm_backend.cpp) |
| `src/sherpa_asr_demo.cpp` | [src/apps/sherpa_asr_demo.cpp](../src/apps/sherpa_asr_demo.cpp) |
| `src/sherpa_onnx_asr_backend.cpp` | [src/speech/sherpa_onnx_asr_backend.cpp](../src/speech/sherpa_onnx_asr_backend.cpp) |
| `src/sherpa_onnx_tts_backend.cpp` | [src/speech/sherpa_onnx_tts_backend.cpp](../src/speech/sherpa_onnx_tts_backend.cpp) |
| `src/sherpa_onnx_vad_audio_recorder.cpp` | [src/speech/sherpa_onnx_vad_audio_recorder.cpp](../src/speech/sherpa_onnx_vad_audio_recorder.cpp) |
| `src/streaming_answer_backend.cpp` | [src/response/streaming_answer_backend.cpp](../src/response/streaming_answer_backend.cpp) |
| `src/streaming_sentence_buffer.cpp` | [src/voice/streaming_sentence_buffer.cpp](../src/voice/streaming_sentence_buffer.cpp) |
| `src/voice_assistant.cpp` | [src/voice/voice_assistant.cpp](../src/voice/voice_assistant.cpp) |
| `src/voice_input_pipeline.cpp` | [src/voice/voice_input_pipeline.cpp](../src/voice/voice_input_pipeline.cpp) |
| `src/voice_performance_report.cpp` | [src/observability/voice_performance_report.cpp](../src/observability/voice_performance_report.cpp) |
| `src/voice_performance_statistics.cpp` | [src/observability/voice_performance_statistics.cpp](../src/observability/voice_performance_statistics.cpp) |
| `src/wav_audio_reader.cpp` | [src/audio/wav_audio_reader.cpp](../src/audio/wav_audio_reader.cpp) |
| `src/wav_audio_recorder.cpp` | [src/audio/wav_audio_recorder.cpp](../src/audio/wav_audio_recorder.cpp) |
| `src/zmq_rep_server.cpp` | [src/apps/zmq_rep_server.cpp](../src/apps/zmq_rep_server.cpp) |
| `src/zmq_req_client.cpp` | [src/apps/zmq_req_client.cpp](../src/apps/zmq_req_client.cpp) |
| `src/zmq_text_client.cpp` | [src/transport/zmq_text_client.cpp](../src/transport/zmq_text_client.cpp) |
