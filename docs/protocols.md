# 通信与数据格式

系统没有 HTTP REST API。现有外部接口主要为 ZeroMQ 消息、CLI、JSON/JSONL 文件和 Linux IIO sysfs。源码 API 见[接口参考](api-reference.md)。

## 1. 进程和端口

| 服务/客户端 | 默认服务端端口 | 传输/用途 | 实现 |
| --- | --- | --- | --- |
| C++ rag_server / rag_client | 5555 | REQ/REP，原始查询文本 → 手册回答文本 | [rag_server.cpp](../src/apps/rag_server.cpp) |
| zmq_rep_server / zmq_req_client | 5555 | 最小 ZeroMQ 联通示例 | [src/apps](../src/apps/) |
| PythonRagServer | 5556 | REQ/REP，兼容文本 RAG JSON 结果 | [python_rag_server.py](../python/rag/python_rag_server.py) |
| PythonRagServer 流接口 | 5557 | DEALER/ROUTER，RAG v1 JSON 事件流 | 同上 |
| Python RAG 控制服务 | 5558 | REQ/REP，取消 RAG 并传播到生成器 | [rag_control_server.py](../python/rag/rag_control_server.py) |
| C++ llm_server 普通接口 | 8899 | REQ/REP，LLM v1 JSON | [llm_server.cpp](../src/apps/llm_server.cpp) |
| C++ llm_server 流接口 | 8900 | DEALER/ROUTER，LLM v1 JSON 事件流 | 同上 |
| C++ llm_server 控制接口 | 8901 | 独立 REP 线程，取消活动生成 | [llm_control_server.cpp](../src/transport/llm_control_server.cpp) |

5555 两种演示服务不能同时占用同一地址。表中是代码默认值，CLI/config 可覆盖。服务端默认 `tcp://*:<port>`、本机客户端使用 `tcp://127.0.0.1:<port>`。服务逻辑以单活动生成为基线，ROUTER 并不自动提供多模型并发调度。

流式客户端通过 DEALER 发送一个 UTF-8 JSON 帧；ROUTER 收到 identity + payload 两帧，回应也以相同 identity 路由 JSON。不要添加 REQ 的空分隔帧。取消走独立通道，使推理阻塞时仍可接收控制请求。

板端 C++ 主链路直接调用本地后端，不需要启动这些网络进程。

## 2. LLM v1

请求由 `LlmProtocol` 编解码：

```json
{"version":1,"type":"generate","request_id":"llm-001","prompt":"请简短回答","stream":false}
```

`request_id` 和 `prompt` 不可空，`stream` 默认 false。普通结果：

```json
{"version":1,"type":"generation_result","ok":true,"request_id":"llm-001","answer":"你好。","backend":"mock","error":"","elapsed_ms":1.2,"finished":true}
```

流式请求发送至 8900，设置 `stream=true`。事件类型为 `generation_chunk/generation_finished/generation_error`，字段为 version、type、ok、request_id、delta、answer、backend、error、sequence、elapsed_ms、finished。

| 事件 | 核心约束 |
| --- | --- |
| generation_chunk | ok=true、delta 非空、finished=false |
| generation_finished | ok=true、answer 非空、finished=true |
| generation_error | ok=false、error 非空、finished=true |

sequence 从 0 递增。完整答案在 finished 中提供，不能把 finished.answer 再追加到已积累的 delta 后面。generateStream 的基类实现可能只在生成完成后回调一次；原生逐 token 能力取决于实际后端。

## 3. RAG v1

请求由 `RagStreamProtocol` 与 Python `rag_stream_protocol.py` 对齐：

```json
{"version":1,"type":"rag_query","request_id":"rag-001","query":"空调怎么打开","stream":true}
```

必须是 JSON 对象，version=1，stream 必须为 true，request_id/query 为非空字符串。以下展示单个 chunk 和完成事件；若只有一个 chunk，终态 sequence 为 1：

```json
{"version":1,"type":"rag_chunk","ok":true,"request_id":"rag-001","sequence":0,"delta":"请先查看空调控制面板。","answer":"","backend":"python_rag","llm_backend":"mock","error":"","elapsed_ms":10.0,"finished":false}
{"version":1,"type":"rag_finished","ok":true,"request_id":"rag-001","sequence":1,"delta":"","answer":"请先查看空调控制面板。","backend":"python_rag","llm_backend":"mock","error":"","elapsed_ms":12.0,"finished":true}
```

`rag_error` 使用 ok=false、非空 error、finished=true，其余字符串字段仍提供。C++ decoder 检查字段类型、非负耗时、sequence 和事件状态组合；客户端进一步检查活动 request_id 及连续 sequence。超时或流异常返回失败，不应把已收到的部分文本当作完整成功。

旧 Python 非流式服务使用原始查询字符串，JSON 结果含 `generated_answer` 等检索/生成信息，由 `RagResponseParser::extractAnswerOrRaw` 兼容提取。它与上述严格 v1 事件流不是同一个协议，不能交换端口直接调用。旧 C++ rag_server 则返回文本。

## 4. 取消接口

LLM 和 RAG 控制服务共用 `RagControlProtocol` 的格式：

```json
{"version":1,"type":"cancel","request_id":"rag-001"}
{"version":1,"type":"cancel_result","ok":true,"cancelled":true,"request_id":"rag-001","active_request_id":"rag-001","error":""}
```

`ok=true,cancelled=false` 可表示请求格式有效，但没有匹配的可取消活动任务。失败响应必须 error 非空且 cancelled=false。request_id 不可为空或全空白。取消通常是协作请求，已经完成的播放或设备写入不会回滚。

## 5. Agent 动作与工具

Planner 输出一个 JSON 对象：

```json
{"type":"tool_call","tool_call":{"id":"call-1","name":"get_cabin_environment","arguments":{}}}
{"type":"final_answer","answer":"当前温度为28.5摄氏度。"}
```

第二行只有在收到可信 Observation 后才可据此宣称读数或控制结果。示例用于展示格式，不是固定设备事实。Planner 解析器、注册表和工具层负责不同检查；它们没有通用外部 Shell 执行入口。

| 工具名称 | arguments | 成功 data 中的核心字段 | 确认 |
| --- | --- | --- | --- |
| get_cabin_environment | {} | temperature_c、humidity_percent | 否 |
| check_cabin_temperature_condition | operator、threshold_c | temperature_c、operator、threshold_c、matched | 否 |
| get_air_conditioner_state | {} | enabled、indicator_led_on | 否 |
| set_air_conditioner | enabled:boolean | enabled、indicator_led_on | 是 |
| set_air_conditioner_if_temperature | operator、threshold_c、enabled:boolean | temperature_c、matched、target_enabled、control_executed、state_before/state_after/state_changed、enabled 等 | 是 |

operator 只允许 `gt/ge/lt/le`；threshold_c 是有限数值并由工具检查范围；拒绝额外键、缺字段和错误类型。只读工具要求空对象。温度阈值以摄氏度表示，工具实现见 [vehicle_tools.cpp](../src/agent/vehicle_tools.cpp)。

条件控制的正确流程是：模型提出原子条件工具 → Executor 保存 Pending → 用户确认 → 工具读取当时温度并判断 → 必要时控制、读取状态 → 把 Observation 交回 Planner。条件未满足时也可成功，需结合 matched/control_executed 解释，不能仅看 ok。

`ToolRegistry::definitions()` 是 C++ 运行时工具 schema 的来源。`planner_format.py` 中有训练侧定义，新增工具要同时维护两侧并检查提示词、数据与执行语义一致性。

## 6. 手册与 Dense 索引

`python/rag/build_index.py` 将车辆手册和 aliases 转为 chunks JSON。每块使用 chunk_id、title、content、text、aliases、retrieval_text；原始手册事实与召回同义词分开。C++ 通过 `loadDocumentChunks` 读取。

BGE ONNX 输入为 `input_ids/attention_mask/token_type_ids`，int64 [batch,sequence]；输出 `embeddings` 为 float32 [batch,512]，模型包装层执行 CLS pooling 与 L2 normalize。导出使用 opset 17。C++ 查询按模型输入契约构造张量。

Dense 产物由 `python/embedding/build_dense_index.py` 生成：

| 文件 | 格式与用途 |
| --- | --- |
| embeddings.f32 | 小端 float32，逐行连续存放归一化向量，长度 rows × dimension × 4 字节 |
| index_meta.json | format=edge_voice_rag_dense_index、version=2、dtype=float32_le、rows/dimension、chunk_ids、pooling、normalized、max_length、query_instruction、指纹 |

元数据保存 chunks/model/tokenizer/embeddings SHA256，并保存 chunks FNV-1a；Python verify 工具负责更完整的文件一致性检查，C++ loader 按实现执行格式/文件长度/映射及 chunks 指纹校验，不能宣称运行时验证了所有 SHA256。

更换 tokenizer、pooling、归一化、查询指令或模型精度版本时，要重新评估文档向量是否仍兼容，并为不同实验保存独立目录，避免覆盖基线。

## 7. Planner 数据与性能日志

训练数据是 JSONL，一行一个记录，包含 id、prompt、response 与场景等元数据；实际字段以 `planner_format.py` 和对应生成脚本为准。v3 训练/验证划分和 heldout 不应混用。QLoRA 输出 adapter，不是直接可加载的 .rkllm 模型。

板端打印 `[PERF_JSON] <json>`，报告 version=1、type=voice_turn_performance，含采集/ASR/检索/LLM/TTS/播放和首文本/首音频/首次调用播放等时序。整轮耗时：

```text
turn_elapsed_ms = capture_elapsed_ms + asr_elapsed_ms + assistant_total_elapsed_ms
```

answer_backend、TTS、playback 在 assistant 内存在流水重叠，不能再累加到整轮时间。first_playback_start_ms 测量“调用播放”的时刻，不是声学设备实际发声的精确时刻。缺少 observed 标志时不能把默认 0 当观测值。

`AGENT_JSON` 记录 Agent 请求、状态、工具/Observation/trace；它不是音频或算子 profiling。后续新增失败率监测应包含所有开始/结束请求，不能只统计成功产生 PERF_JSON 的轮次。
