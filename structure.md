# 仓库目录

```text
EdgeVoiceRAG/
├── AGENTS.md           # 仓库智能体协作与开发验证约定
├── src/
│   ├── apps/           # 所有 main()：文本、板端、服务、联调示例
│   ├── agent/          # 意图路由、Planner、Executor、工具注册与工具实现
│   ├── audio/          # ALSA PCM/录放音、WAV 输入
│   ├── speech/         # sherpa-onnx ASR、TTS、VAD 适配
│   ├── voice/          # 连续会话、语音流水线、分句与工作线程
│   ├── retrieval/      # BM25、BGE、Dense、Hybrid、加载和过滤
│   ├── inference/      # LLM 后端与工厂：Mock / RKLLM
│   ├── response/       # 查询分类、响应策略、本地回答后端
│   ├── transport/      # ZeroMQ 客户端、JSON 协议、LLM 服务与取消服务
│   ├── devices/        # Mock / Linux IIO 设备实现
│   ├── observability/  # 语音性能报告与统计
│   ├── config/         # 文本/板端配置与命令行解析
│   └── common/         # 日志、单调计时、文件指纹
├── include/            # 公共 C++ API，保留现有包含路径
│   └── agent/          # Agent 与设备公共接口
├── python/
│   ├── rag/            # Python RAG 服务、语音实验、Mock/Ollama/ZMQ
│   ├── embedding/      # BGE ONNX 导出、向量索引构建与校验
│   ├── agent_finetune/ # Planner 数据、QLoRA、评测
│   └── tests/          # pytest
├── tests/              # C++ 单元/工作流/并发生命周期/压力/可选模型测试
│   └── support/        # 测试专用检查点与受控音频/生成Mock
├── tools/              # 检索benchmark、BGE分段、离线基线记录器、语音日志统计
├── benchmarks/         # 检索质量与拒答评测用例
├── config/             # 文本客户端、PC/WSL/板端语音运行配置
├── scripts/            # 构建索引、启动服务、冒烟测试脚本
├── kernel_modules/     # 独立内核模块，不属于用户态 CMake 目标
│   └── edge_dht11/     # GPIO IRQ + IIO 温湿度驱动
├── cmake/              # 构建开关、诊断插桩、SDK依赖查找和toolchains交叉编译入口
├── third_party/        # tokenizers-cpp 子模块、外部 SDK
├── docs/               # 项目文档、车辆手册及检索同义词
│   └── diagrams/       # 13张调用/线程/性能/驱动SVG图与标准库绘图脚本
├── data/agent_planner/  # Planner 训练/验证/heldout JSONL
├── models/             # 本地模型资产（Git 忽略）
├── vector_db/          # chunks、Dense 元数据与向量（Git 忽略）
└── build*/             # 本地构建产物
```

实现文件的完整列表见 [source-map.md](docs/source-map.md)。CMake 已分为根装配、各模块子目录、apps/tests/tools；继续使用显式源文件列表和原有 `edge_voice_rag_core` 目标。详见[CMake结构](docs/cmake-structure.md)，后续按依赖关系拆库见[演进报告](docs/technology-roadmap.md)。

板端交付辅助工具：`tools/prepare_rk3588_perf.py` 生成独立部署包；`tools/run_retrieval_baseline.py` 支持交叉清单来源。操作与图示见[RK3588手动性能测试](docs/rk3588-performance.md)，接线见[DHT11](docs/hardware-dht11.md)。

`tools/dht11_latency_probe.py` 为Python标准库板端诊断脚本，不是RAG运行时服务；配套[失败边沿分析](docs/experiments/2026-11-09-dht11-failed-edges.md)。

`src/apps/agent_demo.cpp` 为Rule Planner文字设备入口，默认Mock，支持 `--device iio` 读取真实温湿度；不启动语音SDK或模型。独立ARM验证包和调用步骤见[主线联调](docs/board-agent-bringup.md)。

`src/apps/audio_output_check.cpp` 为独立语音输出探针，复用ALSA播放器及可选TTS；固定测试音、重复播放和WAV导出用于板端分层定位。生成条件及使用步骤见[语音输出验收](docs/board-audio-output.md)。

TTS诊断入口为src/apps/tts_benchmark.cpp（需SHERPA_TTS），与主程序/输出探针共用src/config/board_tts_config.cpp；固定文本位于benchmarks/tts_cases.json。步骤见[TTS性能优化](docs/tts-optimization.md)。
