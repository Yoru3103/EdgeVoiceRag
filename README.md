# EdgeVoiceRAG

EdgeVoiceRAG 是一个面向智能座舱场景的端侧离线语音助手项目，主线运行平台为 RK3576。

项目使用 C++17 搭建板载主流程，覆盖离线语音识别、车辆手册 RAG、本地大模型、语音合成、播放打断以及受约束的单 Agent 工具工作流。当前 Agent 硬件层使用 Mock 设备模拟 DHT11 温湿度数据和空调指示灯状态，后续可以在不改变上层 Agent 架构的情况下替换为真实 Linux 设备实现。

当前板载入口：

```text
src/board_voice_main.cpp
```

## 一、项目目标

本项目主要用于学习和实践以下方向：

- 嵌入式 Linux 上的离线语音助手架构
- C/C++ 应用层模块设计与资源管理
- RKLLM 端侧大模型部署与调用
- 车辆手册的本地 RAG 检索
- AI Agent 工具调用和任务编排
- 设备控制的权限校验、用户确认和安全兜底
- ASR、RAG、LLM、TTS 和播放链路的性能分析
- 模块化硬件抽象和可测试的 Mock 实现

## 二、当前板载主路线

```text
ALSA 录音
    ↓
Silero VAD
    ↓
SenseVoice ASR
    ↓
AgentRoutingAnswerBackend
    ├── 紧急安全问题
    │      ↓
    │   Safety Response + 车辆手册检索
    │
    ├── 实时设备查询或控制
    │      ↓
    │   Agent Planner
    │      ↓
    │   Tool Registry
    │      ↓
    │   参数校验 / 用户确认
    │      ↓
    │   Mock Vehicle Device
    │
    └── 手册或普通问答
           ↓
        ResponsePolicy
        ├── safety
        ├── direct_rag
        ├── rag_llm
        ├── llm_only
        └── clarification
    ↓
流式分句
    ↓
sherpa-onnx VITS TTS
    ↓
ALSA 播放
    ↖ 播放期间可通过 VAD 打断，并取消生成任务
```

主流程采用单 C++ 进程运行，减少板端跨进程通信开销。旧版 C++/Python/ZeroMQ 路线仍保留用于开发、对比测试和兼容性验证，但不再是 RK3576 的主要运行路线。

## 三、分级响应策略

普通问答由 `ResponsePolicy` 根据问题类型和检索置信度进行分级：

| 模式 | 使用场景 | 处理方式 |
|---|---|---|
| `safety` | 制动、转向、起火等安全问题 | 固定安全提示，并附加相关手册内容 |
| `direct_rag` | 高置信车辆事实问题 | 直接播报车辆手册结果，不调用 LLM |
| `rag_llm` | 有相关资料但需要归纳 | 将检索结果交给 RKLLM 生成回答 |
| `llm_only` | 创作或普通开放问题 | 不检索车辆手册，直接调用 LLM |
| `clarification` | 意图不明确且没有可靠资料 | 请求用户补充信息 |

相关阈值位于板端配置文件：

```ini
relevance_minimum_sparse_score=6.0
relevance_minimum_dense_similarity=0.40

response_direct_minimum_sparse_score=8.0
response_direct_minimum_dense_similarity=0.55
```

`direct_rag` 使用更严格的阈值，避免低相关手册片段绕过 LLM 后直接播报。

## 四、Agent 工作流

### 4.1 Agent 的组成

本项目中的 Agent 不是一个单独训练的模型，而是一套受约束的任务执行系统：

```text
用户目标
    ↓
Planner 规划下一步
    ↓
结构化 ToolCall
    ↓
Executor 校验和编排
    ↓
Tool Registry 白名单
    ↓
设备执行
    ↓
Observation
    ↓
继续规划或生成最终回答
```

各模块职责如下：

| 模块 | 职责 |
|---|---|
| `AgentIntentClassifier` | 判断请求是否需要实时设备能力 |
| `RuleAgentPlanner` | 为 Mock 测试提供确定性规划 |
| `LlmAgentPlanner` | 使用本地 LLM 生成结构化工具调用 |
| `AgentExecutor` | 控制最大步数、确认状态和执行循环 |
| `ToolRegistry` | 管理允许调用的工具白名单 |
| `AgentTool` | 将设备能力封装成结构化工具 |
| `VehicleDevice` | 隔离 Agent 和具体硬件实现 |
| `AgentAnswerBackend` | 将 Agent 适配到项目的流式回答接口 |
| `AgentRoutingAnswerBackend` | 在 Agent 与原有问答策略之间路由 |

### 4.2 当前工具

| 工具 | 类型 | 参数 | 是否确认 |
|---|---|---|---|
| `get_cabin_environment` | 读取 | 无 | 否 |
| `get_air_conditioner_state` | 读取 | 无 | 否 |
| `set_air_conditioner` | 写入 | `enabled: bool` | 是 |

示例工具调用：

```json
{
  "type": "tool_call",
  "tool_call": {
    "id": "call-1",
    "name": "set_air_conditioner",
    "arguments": {
      "enabled": true
    }
  }
}
```

### 4.3 多步任务示例

```text
用户：如果车内温度超过26度，就打开空调

Planner：调用 get_cabin_environment
Observation：温度28.5℃，湿度60%

Planner：调用 set_air_conditioner(enabled=true)
Assistant：即将开启模拟空调，请确认是否执行

用户：确认
Tool：模拟空调状态设为开启，指示灯点亮
Observation：enabled=true

Planner：生成最终回答
Assistant：车内温度超过26摄氏度，模拟空调已经开启
```

### 4.4 Agent 安全边界

LLM 只负责提出工具调用，不能直接访问 GPIO、设备文件或 Shell。

确定性 C++ 层负责以下安全措施：

- 工具名称必须存在于 `ToolRegistry` 白名单
- 工具参数必须通过类型和范围校验
- 未注册工具不能执行
- 写操作必须经过用户明确确认
- 待确认操作超过配置时间后自动取消
- 工具写入后重新读取状态进行验证
- Agent 工作流具有最大执行步数
- 紧急安全问题优先于普通设备操作
- 紧急问题会清除之前遗留的待确认控制操作
- 语音打断可以取消待确认任务和 LLM 生成
- 工具调用、参数和 Observation 保存在执行轨迹中

核心原则：

> LLM 可以提出操作，但确定性代码决定该操作是否允许执行。

## 五、Agent 路由规则

当前路由示例：

| 用户输入 | 路由结果 |
|---|---|
| 车内温度是多少 | Agent 环境查询 |
| 空调现在开着吗 | Agent 状态查询 |
| 打开空调 | Agent 设备控制，需要确认 |
| 如果温度超过26度就打开空调 | Agent 多步工作流 |
| 空调怎么打开 | 车辆手册 RAG，不执行控制 |
| 空调为什么不制冷 | 车辆手册或故障知识 |
| 制动系统故障，非常危险 | Safety 安全响应 |
| 给我讲个故事 | LLM Only |

区分“执行命令”和“询问方法”是当前路由层的重要职责：

```text
打开空调      → 请求执行设备操作
空调怎么打开  → 询问车辆功能使用方法
```

## 六、Mock 硬件层

当前使用 `MockVehicleDevice`：

```text
温度：28.5℃
湿度：60%
空调状态：关闭
指示灯状态：熄灭
```

上层只依赖 `VehicleDevice` 接口：

```cpp
class VehicleDevice {
public:
    virtual ~VehicleDevice() = default;

    virtual CabinEnvironment readEnvironment() const = 0;
    virtual bool setAirConditionerEnabled(bool enabled) = 0;
    virtual bool airConditionerEnabled() const = 0;
};
```

后续接入真实硬件时，可以增加 `LinuxVehicleDevice`：

```text
readEnvironment()
    → Linux IIO DHT11接口

setAirConditionerEnabled()
    → Linux LED Class或GPIO接口
```

Planner、Executor、Tool、RAG、ASR和TTS层不需要改变。

## 七、主要目录

```text
include/agent/
├── agent_types.h
├── agent_tool.h
├── tool_registry.h
├── agent_planner.h
├── rule_agent_planner.h
├── llm_agent_planner.h
├── agent_executor.h
├── agent_answer_backend.h
├── agent_intent_classifier.h
├── agent_routing_answer_backend.h
├── vehicle_device.h
└── vehicle_tools.h

src/agent/
├── tool_registry.cpp
├── rule_agent_planner.cpp
├── llm_agent_planner.cpp
├── agent_executor.cpp
├── agent_answer_backend.cpp
├── agent_intent_classifier.cpp
├── agent_routing_answer_backend.cpp
├── vehicle_device.cpp
├── vehicle_tools.cpp
└── agent_demo.cpp

tests/
├── agent_executor_tests.cpp
├── llm_agent_planner_tests.cpp
├── agent_workflow_tests.cpp
├── agent_answer_backend_tests.cpp
└── agent_routing_answer_backend_tests.cpp
```

其他重要模块：

| 路径 | 作用 |
|---|---|
| `src/board_voice_main.cpp` | RK3576板载语音助手主入口 |
| `src/response_policy.cpp` | 普通问答的分级策略 |
| `src/policy_routing_answer_backend.cpp` | RAG、LLM和安全回答执行 |
| `src/retriever_runtime.cpp` | BM25、Dense、Hybrid检索装配 |
| `src/continuous_voice_session.cpp` | 连续语音会话和打断处理 |
| `config/board_rk3576.conf` | RK3576运行配置 |

## 八、构建与测试

### 8.1 基础构建

```bash
cmake -S . -B build
cmake --build build -j2
```

### 8.2 Agent Mock演示

```bash
cmake --build build --target agent_demo -j2
./build/agent_demo
```

建议依次输入：

```text
车内温度是多少
空调状态
打开空调
确认
关闭空调
取消
```

### 8.3 Agent测试

```bash
cmake --build build \
    --target agent_executor_tests \
             llm_agent_planner_tests \
             agent_workflow_tests \
             agent_answer_backend_tests \
             agent_routing_answer_backend_tests \
    -j2

ctest --test-dir build \
    -R "agent_.*tests" \
    --output-on-failure
```

### 8.4 全量测试

```bash
ctest --test-dir build --output-on-failure
```

### 8.5 板端目标

在ALSA、sherpa-onnx、RKLLM和模型路径均已正确配置后构建：

```bash
cmake --build build --target board_voice_assistant -j2
```

运行：

```bash
./build/board_voice_assistant config/board_rk3576.conf
```

## 九、关键配置

RK3576主配置位于：

```text
config/board_rk3576.conf
```

主要配置包括：

```ini
retrieval_backend=hybrid

relevance_filter_enabled=true
relevance_minimum_sparse_score=6.0
relevance_minimum_dense_similarity=0.40

response_direct_minimum_sparse_score=8.0
response_direct_minimum_dense_similarity=0.55

llm_backend=rkllm
llm_model_path=models/llm/qwen2.5-1.5b-rk3576.rkllm

llm_max_new_tokens=256
llm_max_context_len=2048

agent_planner=auto
agent_max_steps=4
agent_confirmation_timeout_ms=30000
agent_mock_temperature_c=28.5
agent_mock_humidity_percent=60.0
```

PC联调时使用Mock LLM和规则Planner；RK3576使用RKLLM和LLM Planner。普通 `MockLlmBackend` 不生成工具调用JSON，因此不能直接作为LLM Planner使用。

## 十、性能与可观测性

每轮对话会输出响应策略信息：

```text
[POLICY] mode=... category=... confidence=... retrieved=... reason=...
```

并通过 `PERF_JSON` 记录主要耗时：

- ASR耗时
- 检索耗时
- LLM首Token耗时
- LLM完整生成耗时
- TTS耗时
- 播放耗时
- 端到端耗时

Agent执行结果还包含：

- 执行的工具名称
- 结构化工具参数
- Observation
- 多步工作流Trace
- 等待确认、完成或失败状态

## 十一、已知不足

### 11.1 规则意图路由存在局限

当前 `AgentIntentClassifier` 使用关键词和固定短语区分实时设备操作与车辆手册问题。这是为了建立一个行为明确、能够自动测试的MVP基线，也用于降低本地小模型误触发设备操作的风险。

当前规则可以稳定区分：

```text
打开空调      → Agent控制
车内温度      → Agent查询
空调怎么打开  → 车辆手册RAG
```

但关键词方案存在以下不足：

- 同义词和口语表达覆盖有限
- 对否定句、反问句和隐含意图处理较弱
- 容易受到ASR识别错误影响
- 缺少完整的会话上下文理解
- 设备能力增加后规则维护成本上升
- 相似表达之间可能发生规则冲突

例如下面的表达可能无法稳定处理：

```text
帮我凉快一点
车里像蒸笼一样
别让空调继续工作了
把刚才那个操作停掉
温度舒服一些就行
```

### 11.2 当前仍是Mock设备

DHT11和空调指示灯尚未接入当前主路线。现阶段只验证Agent架构、工具调用、确认机制和任务编排。

### 11.3 单板单会话

当前板载应用使用固定Agent会话ID，适合单设备、单用户的本地语音交互。未来如果增加远程客户端或多用户，需要在请求协议中加入明确的会话ID。

### 11.4 LLM结构化输出稳定性

当前LLM Planner通过提示词要求模型输出JSON，再由C++解析和校验。小模型仍可能生成非法JSON、未知工具或错误参数。当前系统会拒绝这些输出，但任务会失败。

### 11.5 尚未达到量产标准

当前项目是嵌入式AI学习与验证项目，尚未覆盖完整的权限体系、持久化状态、故障恢复、车规安全认证和生产环境监控。

## 十二、后续升级方向

### 12.1 混合语义路由

后续不应继续无限扩充关键词表，而应升级为：

```text
安全硬规则
    ↓
Pending确认/取消规则
    ↓
高置信设备命令规则
    ↓
BGE Embedding语义路由
    ↓
低置信度LLM Router或请求用户澄清
```

项目已经具备BGE Embedding能力，可以为以下类别准备意图示例：

```text
manual_query
device_query
device_control
general_chat
```

通过查询向量与意图示例向量的相似度完成语义路由，增强同义表达和口语输入的覆盖能力。

即使升级为语义路由，以下能力仍然保留在确定性C++层：

- Safety安全响应
- Tool Registry白名单
- 参数校验
- 写操作确认
- 最大执行步数
- 实际设备访问

### 12.2 真实Linux硬件接入

实现 `LinuxVehicleDevice`，通过Linux标准设备接口读取DHT11并控制LED；上层Agent不需要修改。

### 12.3 约束解码与Schema校验

为RKLLM增加JSON语法约束、JSON Schema统一校验、错误恢复和重试机制，提高工具调用可靠性。

### 12.4 Agent评测集

建立覆盖以下情况的离线评测集：

- 明确设备命令
- 手册问法与设备命令的边界
- 否定句和取消操作
- ASR近音错误
- 多条件任务
- 非法工具调用
- 重复规划和最大步数
- 紧急安全问题

### 12.5 端侧推理优化

继续测量并优化：

- RKLLM首Token延迟
- Agent多步任务总耗时
- ASR和TTS线程数
- 模型量化效果
- 内存峰值
- 冷启动时间

## 十三、项目亮点

- 在RK3576上构建离线语音交互主线
- 将车辆手册RAG和实时设备Agent组合在同一响应系统中
- 支持BM25、Dense和Hybrid检索
- 使用策略分级降低不必要的LLM调用
- 实现结构化ToolCall、Observation和多步任务编排
- 使用确定性C++保护设备控制边界
- 支持用户确认、取消、打断和最大执行步数
- 通过Mock设备和Scripted LLM完成可重复的自动化测试
- 记录ASR、检索、LLM、TTS和端到端性能
- 通过硬件抽象为DHT11、LED和更多设备预留扩展能力

## 十四、面试介绍参考

可以将项目概括为：

> 基于RK3576实现端侧离线车载语音助手。系统在C++主进程中集成SenseVoice ASR、混合检索RAG、RKLLM和sherpa-onnx TTS，并实现受约束的单Agent工作流。LLM负责生成结构化工具调用，C++ Executor负责工具白名单、参数校验、多步编排、写操作确认和取消机制；当前通过Mock温湿度传感器和空调指示灯验证设备交互，并通过抽象接口为后续Linux驱动接入预留扩展空间。

面试时建议重点说明以下设计取舍：

1. 为什么“空调怎么打开”不能直接执行设备操作。
2. 为什么LLM只负责规划，不能直接访问GPIO或Shell。
3. 为什么使用规则路由建立确定性基线。
4. 如何通过Tool Registry、确认机制和最大步数控制风险。
5. 如何将Mock设备替换为真实Linux硬件而不修改Agent上层。
6. 如何使用BGE语义路由改进关键词分类的局限。

## 十五、项目状态

当前已完成：

- 板端离线ASR、LLM、TTS和播放主线
- 连续语音会话与播放期间打断
- BM25、Dense、Hybrid检索
- 检索相关性过滤和分级响应策略
- Agent工具接口和工具注册中心
- Rule Planner和LLM Planner
- 多步Agent执行循环
- 写操作确认与取消
- 待确认操作自动超时
- Agent流式回答适配
- Agent与原有RAG策略的外层路由
- Agent Planner、最大步数和Mock环境参数配置化
- `AGENT_JSON`结构化执行审计日志
- Mock车辆环境与空调状态工具
- Agent单元测试和工作流测试

当前待完成：

- DHT11和LED真实硬件实现
- BGE语义意图路由
- 更严格的结构化输出约束
- 板端完整场景性能数据采集

本项目目前适合描述为：

> 一个运行于RK3576的端侧离线语音助手与受约束设备Agent原型，具备车辆手册RAG、多级响应、实时设备工具调用、用户确认、语音打断和完整自动化测试。
