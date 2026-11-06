# 技术栈补充与项目演进报告

日期：2026-11-04。项目：EdgeVoiceRAG。主平台：RK3588；开发与基础验证：PC/WSL。

## 1. 建设目标与结论

建议把项目发展为 **“可复现评测的端侧语音推理与设备 Agent 系统”**：在现有功能上补齐模型精度、算子性能、内存与取消可靠性的工程证据。主线是完善 C++ 并发和资源管理 → 建立性能基线 → 做 PTQ 精度分析 → 接入 RKNN 通用推理 → 研究 Runtime 内存和同步；MLIR 作为可独立演示的编译实验接入。

现有功能已经覆盖较完整的应用链路，下一阶段的价值在于能回答：哪个阶段慢、为什么慢，量化误差从哪里来，迁移后是否保持质量，数据拷贝与同步花了多少时间。每增加一种技术，都应该有代码、实验和可复现结果。

本次已完成源码目录整理、项目/模块/接口/协议/构建文档，并修复一个重构前已存在的测试格式依赖。下文的 PTQ、RKNN、MLIR、DMA 等均为**后续计划**，尚未因写入本报告而实现。

## 2. 图片技术栈与现状映射

| 图片方向 | 优先级 | 当前已有基础 | 主要缺口 | 项目内补充内容 |
| --- | --- | --- | --- | --- |
| C++11/14/17、STL、内存管理、并发、Linux 调试 | P0 | C++17、RAII/PImpl、unique/shared_ptr、mutex/CV、atomic、future、有界队列和取消 | 压力下生命周期证据、Sanitizer、统一错误边界、组件依赖边界 | 会话取消压力实验、资源检查、分模块 CMake、GDB 调试记录 |
| 模型量化与精度分析 | P0 | INT8 语音资产、W8A8 命名的 RKLLM 模型、QLoRA 训练脚本、BGE FP32 ONNX 导出 | 自建 PTQ、校准集、scale/zero-point 分析、逐层误差、业务精度对照 | BGE FP32→INT8 PTQ、张量误差定位、检索退化分析与混合精度 |
| Profiling 与性能优化 | P0 | PERF_JSON、首文本/音频时序、检索 benchmark、P50/P95 汇总 | 算子 trace、CPU/NPU 可归因测量、内存峰值、拷贝/排队耗时 | ORT profiling + Linux perf + RSS + RKNN 性能查询，形成优化前后对照 |
| ONNX、ONNX Runtime、RKNN | P1 | BGE ONNX 导出/校验、ORT C++ CPU 推理、sherpa 适配、RKLLM 后端 | RKNN-Toolkit2 通用模型转换/运行闭环、兼容性清单、回退策略 | 独立 RKNN smoke test，兼容后再接 BGE 或小型语义路由器 |
| 计算图 IR、MLIR、TVM、LLVM | P1 | 已有可观测的 ONNX 子图和数值对照基础 | IR/Lowering/Pass 实作与验证 | 独立 MLIR 小型优化 Pass + CPU lowering；有收益再接业务 |
| NPU Runtime、DMA、Cache、IOMMU | P1 | RKLLM SDK 调用、Linux IIO 驱动、GPIO IRQ/completion/mutex | 通用 NPU buffer 生命周期、拷贝与绑定、cache 同步、地址转换理解 | RKNN buffer 复用/绑定对照，记录真实同步路径与 IOMMU 映射关系 |

QLoRA 中的 4-bit 基座用于训练显存节省，不能直接证明推理 PTQ 或 RKLLM 部署精度。RKLLM 与 RKNN-Toolkit2 是不同部署入口：官方 RKNN 项目将 LLM 指向专门 SDK；本项目应分别维护 LLM 与通用网络路径。[RKNN-Toolkit2 官方仓库](https://github.com/airockchip/rknn-toolkit2)、[RKNN-LLM 官方仓库](https://github.com/airockchip/rknn-llm)

## 3. P0：C++ 工程、内存与并发

### 3.1 从现有代码学习并改进

| 知识点 | 当前入口 | 具体改进任务 | 验收证据 |
| --- | --- | --- | --- |
| RAII 与所有权 | VoiceAssistant、RetrieverRuntime、RKLLM/ALSA PImpl | 画出构造/析构顺序，确认引用对象寿命；逐条验证异常退出、start 失败、重复 stop | 析构日志/调试记录，线程和 SDK handle 全部释放 |
| STL 与移动语义 | AudioBuffer、BoundedBlockingQueue、shared_ptr RequestState | 测量 PCM 拷贝和分配次数；检查 move 在任务转移处是否真正避免大块复制 | 拷贝/分配计数与峰值内存，不只给出代码改动 |
| mutex/CV/atomic | 句子/音频队列、活动请求、设备状态 | 检查锁顺序、关闭唤醒、取消与完成交叉，避免等待时持有后端锁 | 正常完成、满队列、合成失败、播放失败、取消竞态均可退出 |
| future 与取消 | ContinuousVoiceSession、RKLLM abort | 使用受控延迟 Mock 构造“起声→停播→取消→新请求”，验证旧任务不污染新请求 | request_id 关联 trace、打断延迟分布、无残留播放 |
| Linux 调试 | 默认 C++ 测试与进程 | GDB 抓栈、core 分析、perf 热点；分别建立 ASan/UBSan 和 TSan 构建 | 可复现命令、修复前后证据、无未解释的项目内报错 |

建议新增 `CMakePresets.json` 或独立 CMake 选项，先覆盖 host Mock 路线；Sanitizer 分开运行，第三方 SDK 的限制单独记录。不要一开始重写整个线程系统。先写能稳定触发满队列/取消/退出路径的场景，再依据证据改生命周期。

建议压力标准：使用固定种子，对 Mock 流水线执行至少 1000 轮正常/取消混合请求，设置每轮超时；所有线程最终回收，没有旧 request_id 的音频进入下一轮。这个数量是建议验收门槛，不是当前实测值。

### 3.2 目录整理后的下一步拆库

本次已经把 src 分为职责目录，并将根CMake拆为选项、SDK依赖和各模块子CMake；保持原 `edge_voice_rag_core` 单库和 include 路径。构建分层详见[CMake指南](cmake-structure.md)。下一阶段建议按下列顺序拆目标：

```text
edge_common
  ├── edge_retrieval
  ├── edge_inference
  ├── edge_transport
  ├── edge_devices
  └── edge_audio / edge_speech
          ↓
  edge_response / edge_agent
          ↓
      edge_voice
          ↓
         apps
```

拆库前先检查实际头文件依赖，避免按目录机械拆分产生循环。Agent 公共头文件目前引用回答接口，语音结果又引用回答元数据，可先把跨层 value types 放入独立接口目标。只有真正需要的目标才链接 SDK/ZeroMQ，逐步缩小 PUBLIC 依赖。

## 4. P0：建立可解释的性能基线

### 4.1 先定义指标和边界

| 指标 | 当前支持情况 | 建议补充 |
| --- | --- | --- |
| ASR、检索、LLM 首 token/总时间、TTS、播放 | 部分阶段已记录 | 固定计时起点、错误和取消状态，记录模型/配置/输入长度 |
| 首次播放调用、整轮时间 | 已有 | 分别统计“用户说完→开始播报”和包含录音等待的整轮，必要时用回环测实际声学延迟 |
| 句子/音频队列 | 有背压，无完整观测 | 入/出队时刻、深度峰值、生产者等待时长 |
| 内存 | 训练侧有 GPU 峰值，主程序无统一记录 | cold/warm RSS、VmHWM、加载/运行峰值、缓存/队列占用 |
| 算子耗时 | 未记录 | BGE ORT trace、RKNN 支持范围内的性能查询、CPU 栈热点 |
| CPU/NPU | 只有 SDK 使用事实 | 分别记录 CPU 时间、Runtime/device trace、NPU负载采样及传输/等待，不混用指标 |

整轮中 LLM、TTS、播放存在重叠，不能用 CPU 线程耗时除以整轮耗时就声称得到“CPU/NPU 执行占比”。只有同一时间线上的可归因硬件事件才适合分解关键路径；利用率是辅助观测，不能等同算子执行时长。

ORT 官方提供模型/算子 profiling，用于区分 Session 内部执行与应用层开销；本项目可从 `BgeEmbedder::load/encodeQuery` 接入。[ORT profiling 文档](https://onnxruntime.ai/docs/performance/tune-performance/profiling-tools.html)

### 4.2 建议实验

先在 Release/RelWithDebInfo 下固定测试集、模型 hash、CPU 频率策略、线程数、VAD/TTS 配置和温度环境。对每个配置先预热，再采样；冷启动单独启动进程测量。建议至少 30 轮用于初步分布，P95 用更多样本复核，同时保留原始数据。

| 实验 | 改动变量 | 保持不变 | 观察与输出 |
| --- | --- | --- | --- |
| BGE 线程扫描 | intra_op=1/2/4，inter_op固定 | 模型、查询、max_length | P50/P95、CPU时间、RSS、ORT 算子热点 |
| 输入长度扫描 | 16/32/64/128/256/512 桶 | 文本样本来源和 tokenizer | 耗时、截断率、检索准确率；按真实长度分布选择上限 |
| TTS 与 ASR 竞争 | 线程数组合 | 模型和语音样本 | 首次播放、打断延迟、CPU 争用；RK3588 当前 TTS=6，应测而非直接认定最优 |
| 队列容量扫描 | sentence/audio容量 | 同一流式 chunk 节奏 | 背压等待、内存和首声延迟 |
| 策略分流收益 | DirectRag / RagLlm / LlmOnly | 同一问题与质量标注 | 减少的生成次数、质量变化和延迟，按模式分组 |
| RKLLM prompt 长度 | 工具 schema/历史压缩策略 | 模型、问题、生成上限 | TTFT、总时长、JSON有效率和动作准确率 |

完成每次优化都应展示修改点、改善的瓶颈、对照指标和质量回归。即使没有加速，只要定位到真实热点并解释原因，也比无证据的“性能提升”更有面试价值。

## 5. P0：量化与逐层精度分析

### 5.1 为什么先选 BGE

已有 PyTorch→ONNX 导出、Python/ORT 输出对齐、C++ ORT 编码和检索 benchmark，形成了较短的实验闭环。ASR/TTS 第三方 INT8 资产缺少本仓库内的完整 FP32/PTQ 过程；先从 BGE 实现自己的量化流程更容易形成证据。

建议新增（均为拟建路径）：

```text
python/quantization/
  collect_calibration.py
  quantize_bge.py
  compare_embeddings.py
  debug_activations.py
benchmarks/quantization/         # 评测定义，不提交大模型
docs/experiments/quantization.md
models/embedding/<experiment>/  # 本地不同精度产物
```

PTQ 需要理解：`q=clip(round(x/scale)+zero_point)`，反量化为 `x_hat=scale*(q-zero_point)`；误差来自舍入与截断，scale/zero_point、校准范围和逐张量/逐通道选择共同影响结果。逐通道通常用于更细粒度的权重范围，不能假定所有算子/硬件都支持同一种格式。

### 5.2 操作步骤与产物

1. 冻结当前 FP32 模型、tokenizer、pooling、normalize、instruction、max_length 和检索基线。
2. 按实际查询/手册长度与表达采集校准样本；校准集与最终 heldout 隔离，保留来源和版本。
3. 先进行 shape inference/图预处理，再做动态 INT8 与静态 QDQ PTQ 两条对照；记录工具版本、算子选择、量化类型和校准方法。
4. 比较查询及文档 embedding 的误差：最大/平均绝对误差、余弦相似度、向量范数与异常值。单个“余弦很高”不足以证明业务无退化。
5. 使用同一检索集测 Top1、Recall@K、MRR、负例拒答率；检查 DirectRag 阈值跨越引发的路由变化。
6. 对退化样本导出匹配的 FP32/INT8 中间激活，比较权重和激活范围，定位误差突增层；检验 LayerNorm/Softmax/MatMul 等是否为实际敏感点。
7. 对可定位的敏感节点排除量化或改量化配置，重新评测；每次只改变少量变量。
8. 最后才接 C++ 并比较延迟、模型大小、RSS和冷启动；INT8 不保证在所有 CPU/形状上加速。

ORT 官方量化工具区分预处理、动态/静态量化和激活/权重对比；它建议把图优化放到量化前，便于定位精度损失。[ORT 量化与调试文档](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html)

### 5.3 索引一致性实验

至少保留三组，避免把模型量化和索引重建混为一个变量：

| 查询编码器 | 文档索引编码器 | 目的 |
| --- | --- | --- |
| FP32 | FP32 | 冻结基线 |
| INT8 | FP32 | 测试只替换在线查询是否可接受 |
| INT8 | INT8 | 测试整条向量空间一致部署 |

不同模型的向量目录分开保存，重新校验指纹、维度和输入契约。建议验收门槛可设为“heldout Top1/Recall 相对基线下降不超过预先约定的 1 个百分点，负例误召回不增加超过约定值”，但应在查看结果前确定，且报告样本量；这是建议质量门槛，不是保证能达到的结论。如果收益不足，保留 FP32 也是合理实验结果。

## 6. P1：ONNX/ORT/RKNN 转换与芯片推理

### 6.1 接入顺序

第一步先做独立 RKNN smoke test，选择当前 SDK 明确支持的小型网络。保存输入、CPU golden 输出、转换日志、目标芯片和板端结果，验证 PC 转换→RK3588 Runtime 推理→数值比较闭环。

第二步检查 BGE 的输入 dtype、动态序列长度、Gather、Attention 分解、LayerNorm、Softmax、输出归一化等算子与布局。**BGE 是否适合当前 RKNN SDK 必须通过兼容性实验确认**，不能将 ORT 可执行等同 RKNN 可转换。必要时使用固定 batch=1 和长度桶，记录每次 padding/truncation 的质量成本。

第三步在兼容性和精度通过后，提取 `TextEmbedder` 抽象，让 OrtBgeEmbedder/RknnEmbedder 复用 tokenizer、instruction、pooling 和输出契约；扩展 RetrieverRuntime 的 embedder 选项。不要把全部 BGE 假设写进通用 Runtime。

若 BGE 无法合理转换，选择小型语义意图分类器作为第二个业务入口，用于 `manual_query/device_query/device_control/general_chat` 路由。保留确定性安全/确认规则，并测 ASR 近音、否定句和手册/命令边界；不把设备控制权交给分类分数。

### 6.2 兼容性清单

每个模型记录源框架/opset、动态轴、输入输出名称/shape/dtype、量化格式、unsupported ops、替换方案、Runtime/驱动版本、golden 对照、冷启动/推理/拷贝/后处理耗时、峰值内存。SDK 支持范围以固定版本的官方资料和实际日志为准。[RKNN-Toolkit2 官方仓库](https://github.com/airockchip/rknn-toolkit2)

失败回退应是明确选择的 CPU 路线并记录原因，不能悄悄宣称“全部运行在 NPU”。RKLLM 的模型转换、adapter 合并和部署另建 manifest，不能把 .onnx→.rknn 的流程直接套在 .rkllm 上。

## 7. P1：计算图、MLIR、TVM、LLVM

不建议同时给主程序引入三套编译器依赖。优先选择 MLIR 做一个小而完整的可复现实验，再根据目标岗位深入 TVM 或 LLVM。主程序保留现有构建速度。

拟建 `experiments/mlir/`，从最小 Toy/独立优化工具开始，实现 **双转置消除 Pass**。只匹配已验证的 rank/置换组合，使两次转置复合为恒等映射；不满足约束时必须保留原图。这个实验与模型图中的数据布局优化有关，但不预设当前 BGE 中一定存在该模式。

交付内容：

- Pass 前后 IR：明确 op、type、shape、use-def 变化及匹配条件。
- 正例与负例：可消除、不可消除置换，额外使用者、不同 shape 等语义边界。
- 数值对齐：参考执行与优化执行比较；不因实验方便随意开启 fast-math。
- lowering：从高层图到合适的 lower-level dialect，再到 LLVM/CPU 代码的最小示例。
- 端到端耗时、临时张量/拷贝变化；若只有 IR 简化，没有运行收益，就如实说明。

官方 Toy 教程提供 pattern rewrite 与 lowering 的分阶段示例，可以作为实现起点。[MLIR transformation](https://mlir.llvm.org/docs/Tutorials/Toy/Ch-3/)、[Toy lowering 教程导航](https://mlir.llvm.org/docs/Tutorials/Toy/)

后续只有在实际 BGE/路由模型导出图发现对应机会时，才把这个优化接到离线产物流程；记录优化后 golden 对齐。完成独立 Pass 可以写“实现 MLIR 图变换实验”，尚不能写“完成推理编译器”。

## 8. P1：NPU Runtime、DMA、Cache、IOMMU

DHT11 使用 GPIO IRQ 与 IIO，是学习 Linux 同步和设备抽象的基础，但它不经过 NPU DMA。该方向应以独立 RKNN Runtime 实验补齐。

建议分三组对照：

| 方案 | 学习/改动点 | 必须记录 |
| --- | --- | --- |
| 常规输入/输出 API | host buffer → SDK 输入 → run → outputs | 每阶段耗时、布局转换、实际输出校验 |
| 复用 host/Runtime buffer | 每次分配改为固定池/复用 | 分配次数、冷/热延迟、内存峰值、释放顺序 |
| Runtime 支持的 IO 内存绑定 | create_mem/set_io_mem、stride/dtype、同步 | CPU 写入/提交/完成/CPU 读取时序、cache 操作和版本支持 |

RKNN 官方 API 包含性能查询、buffer 和 cache 同步相关接口，但可用行为要以实际板端 SDK 头文件及驱动为准。性能采集本身会带来开销，需把 profiling run 与正常 benchmark 分开。[RKNN 官方 C API](https://raw.githubusercontent.com/airockchip/rknn-toolkit2/master/rknpu2/runtime/Linux/librknn_api/include/rknn_api.h)

画清以下链路并与代码对照：

```text
CPU 虚拟地址
   → CPU 访问/填充
   → DMA 映射与设备可见地址（可能经 IOMMU）
   → 提交任务与数据可见性同步
   → NPU 完成通知
   → CPU 读取前的必要同步
   → 解绑/释放
```

CPU 虚拟地址、物理地址和设备 DMA 地址不是同一个概念；IOMMU 处理地址映射，不等于自动解决 cache 可见性和执行顺序。Linux DMA API 文档提供地址与一致性模型，用户态应通过 Runtime 暴露的接口管理，不把内核 DMA API 直接搬进业务程序。[Linux DMA API 指南](https://www.kernel.org/doc/html/latest/core-api/dma-api-howto.html)

验收重点是输出正确、可重复、没有 stale data/越界/生命周期错误，再测是否减少拷贝和时间。使用 IO 内存绑定也不能未经追踪就声称“端到端零拷贝”；tokenizer、布局转换、Runtime 内部都可能产生复制。

## 9. 按依赖推进的里程碑

下列工作量仅用于学习排期，按每周约 8–12 小时估计；以验收为准，不按周数强行进入下一阶段。

| 阶段 | 建议时间 | 工作与入口 | 完成标准 |
| --- | --- | --- | --- |
| M0：梳理与可构建基线 | 本次 | src 分类、文档、默认回归 | 已交付；硬件能力边界清楚，入口可定位 |
| M1：C++ 可取消流水线 | 1–2周 | voice/、bounded_blocking_queue、Sanitizer | 取消/队列关闭压力场景可复现，线程回收有证据 |
| M2：Profiling 基线 | 1–2周 | observability/、BgeEmbedder、benchmark | 质量/延迟/内存/热点有同版本基线和原始数据 |
| M3：BGE PTQ 精度闭环 | 2–3周 | python/quantization（拟建） | FP32/INT8/敏感层处理对照，检索质量门槛清楚 |
| M4：RKNN 转换与适配 | 2–3周 | 独立 smoke test → embedder/路由模型 | RK3588 golden 对齐、兼容性日志、CPU回退明确 |
| M5：Runtime 数据路径 | 1–2周 | RKNN buffer/sync 微基准 | 分配、拷贝、同步的可解释测量 |
| M6：MLIR Pass | 2–3周，可在M3后独立 | experiments/mlir（拟建） | Pass/负例/IR/数值/Lowering完整；与主链隔离 |
| M7：集成展示 | 约1周 | 文档、demo、完整板端场景 | 同一版本功能、质量和性能报告可复现 |

建议**下一次只先实施 M1+M2 的最小部分**：加独立诊断构建、固定输入 benchmark manifest、补 BGE profiling 与队列等待计时。这样 M3 有可信基线，后续优化不靠感觉判断。

QLoRA 可保留为并行研究支线：冻结 heldout，比基础模型和 adapter 的 JSON有效率/工具选择/参数/最终回答；随后合并 adapter、按匹配 RKLLM 工具转换并重新测板端。它不阻塞先做 BGE PTQ，也不替代 PTQ。

## 10. 每次实验的报告模板

所有实验按同一结构留档，可放 `docs/experiments/`；模型和大 trace 存本地忽略目录或专门产物存储。

| 项目 | 记录内容 |
| --- | --- |
| 假设 | 具体瓶颈/误差来源，以及改变一个变量后期待观察到什么 |
| 版本 | Git commit + dirty diff、模型/tokenizer/数据 hash、SDK/驱动/内核、Python/编译器 |
| 环境 | RK3588型号、内存、频率策略、温度、线程、构建类型 |
| 输入 | 样本数、长度/类别分布、train/calibration/heldout隔离 |
| 对照 | baseline 与 candidate 的唯一差别、冷启动/预热/重复次数 |
| 结果 | 原始样本、均值/P50/P95、RSS峰值、质量分组、失败/取消数 |
| 解释 | 热点/敏感层证据，是否支持最初假设，下一步最小修改 |

基准输出建议带 run_id、request_id、response_mode、input/output长度和 observed 标志；避免把失败样本从延迟/成功率分母中悄悄删掉。

## 11. 简历内容如何随着阶段增长

现在可依据代码描述：

> 使用 C++17 设计端侧语音助手架构，集成混合检索、可取消的流式回答、分句 TTS/播放流水线与设备 Agent；通过工具白名单、参数验证和用户确认组织设备操作，支持 Mock 与 Linux IIO 设备抽象，并提供单元、协议和工作流测试。

是否写“在 RK3588 完成离线语音运行”应以你的实际部署记录为准；本次验证是 PC/WSL，不能替代板端证据。

后续完成后才填入实测数字：

- M1/M2：指出修复的具体线程/生命周期问题，说明在固定负载下 P95 首播/打断延迟从 [A] 到 [B]，峰值内存从 [C] 到 [D]。
- M3：说明 BGE PTQ 校准、逐层误差定位与敏感层策略，给出模型大小/延迟及 Recall@K 变化。
- M4/M5：说明 RKNN 转换兼容性、CPU golden 对齐、buffer 生命周期和同步，给出传输/推理拆解。
- M6：说明独立实现的 MLIR Pass、匹配条件、IR与数值验证；仅在真实接入并测量后关联业务收益。

[A/B/C/D] 都是待填字段。已经存在模型文件、训练日志或 SDK 包，不足以填入优化成果。每个简历要点最终应能定位到实现、实验记录与可演示场景。
