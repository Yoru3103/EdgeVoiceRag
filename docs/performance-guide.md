# 性能观测与固定输入基线

更新：2026-11-08。本阶段为板端C++主线补应用层观测，并先在PC/WSL固定输入采样。它提供后续RK3588优化的对照方法，不把Host时间当作板端结果。当前实测见[基线记录](experiments/2026-11-08-observability-baseline.md)。

## 1. 这一步回答什么

已有首文本、首音频、首播和各阶段累计时间；新增指标用来区分“生成/推理在执行”与“上游因队列容量不足而等待”。

```text
回答/分句 --[sentence_queue.push容量等待]--> TTS
TTS       --[audio_queue.push容量等待]----> 播放
```

两路等待可能与生成、合成或播放重叠，不能与已有阶段时间直接相加得到总延迟。队列等待不是CPU/NPU执行占比；BGE分段也不是ORT算子trace。

## 2. 请求级队列指标

`VoiceAssistantTiming` 增加句子/音频两组 `VoiceQueueTiming`：

| 字段 | 单位 | 统计含义 |
| --- | --- | --- |
| capacity | 项 | 共享队列配置容量 |
| peak_depth | 项 | 本请求数据push成功时，锁内观测到的共享队列深度峰值 |
| push_wait_count | 次 | 因容量已满而进入CV等待的本请求数据push次数 |
| push_wait_ms | ms | 上述等待的累计墙钟时间，包含CV唤醒后重新获得队列锁 |
| queue_metrics_observed | bool | 该请求是否建立了队列观测上下文 |
| queue_metrics_complete | bool | 是否为正常成功完成后的完整快照 |

容量等待从取得队列锁、确认已满后开始计时，不包含最初获取mutex的等待；没有容量等待时精确记录0。这里只测生产者容量等待，不测消费者空队列等待，也不测任务从入队到出队的驻留时间。

只统计数据push，不统计End控制项push的等待；深度采样仍是整个共享队列的占用，可能包含旧请求残留任务或控制项，不包含正在合成/播放的任务。因此它不是某请求独占队列的大小。

取消/失败请求可能在旧TTS尚未返回时完成，其快照标为不完整；后续工作线程可能继续更新共享状态，这些更新不会反写已经返回的结果。正常请求在End经过播放线程后才结束，数据push指标已记录完整。

`BoundedBlockingQueue::push(value, metrics)` 提供按次观测；原有push重载保持可用。metrics由调用线程写入，异步调用后应等待future再读取。核心队列未启用按次观测的push不会读取计时时钟。

## 3. PERF_JSON兼容与汇总

沿用version=1，新增一组可选字段：

```text
queue_metrics_observed / queue_metrics_complete
sentence_queue_capacity / sentence_queue_peak_depth
sentence_queue_push_wait_count / sentence_queue_push_wait_ms
audio_queue_capacity / audio_queue_peak_depth
audio_queue_push_wait_count / audio_queue_push_wait_ms
```

旧报告完全没有这组字段时仍可读取；出现任何一个扩展字段时，必须提供完整字段组，计数需为非负整数、时间需有限且非负、峰值不能超过容量。

汇总器仅将observed=true且complete=true的两项push_wait_ms加入均值/P50/P95。真实观测到的0ms保留；旧日志缺失或取消后不完整的快照不会被补成0ms。容量、峰值和计数保留在每轮原始日志中，不放进单位为ms的统计表。PERF_JSON采样范围仍是AnswerCompleted，完整取消路径不保证产生一份报告；日志成功率不能当作所有请求的成功率。

`voice_performance_summary`仍使用原有线性插值分位数；下文检索基准与分段采样使用nearest-rank。比较结果时同时保存单位、样本数与分位数方法。

## 4. BGE与Dense显式分段采样

普通 `encodeQuery/searchTopK` 保持原路径，不读取分段时钟。调用 `encodeQueryProfiled`、`searchTopKProfiled` 才采集：

| 指标 | 计时范围 |
| --- | --- |
| lock_wait_ms | BGE互斥锁获取前至获取后 |
| prepare_ms | 加instruction、tokenizer、CLS/SEP/截断、输入数组和ORT Tensor准备 |
| inference_ms | 整个Ort::Session::Run调用；包含模型内算子及Runtime开销 |
| postprocess_ms | 输出校验、向量复制和再次L2归一化；不是模型内部pooling的单独耗时 |
| embedding_total_ms | BGE入口至后处理记录结束；不含后续局部ORT对象析构/锁释放 |
| index_search_ms | DenseVectorIndex搜索 |
| mapping_ms | hit到DocumentChunk映射与过滤 |
| total_ms | Dense入口至结果映射完成；包括BGE返回时的清理开销 |
| token_count | 实际输入token数，含特殊token并已截断 |
| query_bytes | UTF-8字节数；不能当作字符数或token数 |

失败/非法查询的timing.observed=false；复用Dense计时对象时会先清空，避免把上一查询数据带入下一次。测试已检查普通/采样路径向量与排序一致。

这些是应用层墙钟分段。尚未接入ORT逐算子trace、Linux perf热点或NPU利用率采集。

## 5. 构建与直接运行

所有命令在Linux/WSL项目根目录执行，使用Release或RelWithDebInfo。BGE/SDK路径按[开发文档](development.md)配置；启用BGE后新增工具 `bge_retrieval_profile`：

```bash
cmake --build build-board-pc --target bge_retrieval_profile retrieval_benchmark -j4

# Dense应用分段：top_k=3，实测5遍，预热1遍，intra线程4
./build-board-pc/bge_retrieval_profile \
  vector_db/chunks.json benchmarks/retrieval_cases.json \
  models/embedding/bge-small-zh-v1.5 vector_db/bge-small-zh-v1.5 \
  3 5 1 4
```

工具输出一条dense_profile_config和逐条dense_profile_sample JSON；序列化在采样循环结束后执行。inter_op_threads固定1，未开启相关性过滤。该结果用于分解Dense开销，不能与启用过滤的Hybrid端到端时间直接等同。

既有retrieval_benchmark新增可选 `--samples`，原命令保持兼容：

```bash
./build-board-pc/retrieval_benchmark hybrid \
  vector_db/chunks.json benchmarks/retrieval_cases.json 3 5 \
  models/embedding/bge-small-zh-v1.5 vector_db/bge-small-zh-v1.5 \
  --filter --samples
```

LATENCY_SAMPLE包含run、case_id、elapsed_us与result_count；METRICS_JSON继续提供质量和整体分位数。原始样本在测量循环结束后打印，不在单次查询计时区间内输出。基准先做一遍质量评测，再做一遍预热，之后测runs遍；这些策略和默认线程数写入结果。

## 6. 一条命令保存完整基线

记录器是离线工具，只用Python标准库，另需Linux系统的GNU time或BusyBox time（/usr/bin/time）；不需要启动Python RAG服务或安装训练依赖。

```bash
python3 tools/run_retrieval_baseline.py \
  --build-dir build-board-pc --backend hybrid --filter --runs 5 \
  --output perf_logs/my-first-hybrid-baseline

python3 tools/run_retrieval_baseline.py \
  --build-dir build-board-pc --mode profile --backend dense \
  --runs 5 --warmup 1 --threads 4 \
  --output perf_logs/my-first-dense-profile
```

输出目录必须不存在，避免覆盖旧实验。Debug、Sanitizer构建被拒绝；benchmark模式的线程/预热采用既有工具固定设置，profile模式可显式指定。三个后端采样应串行执行，避免相互争用CPU。

| 文件 | 内容 |
| --- | --- |
| manifest.json | 命令、构建缓存、编译器、源码/模型/索引/输入/可执行文件/动态库SHA256、Host/CPU信息、状态与资源统计 |
| stdout.log / stderr.log | 原始工具输出和错误 |
| resource.tsv | 所选time实现的资源原始记录 |
| samples.jsonl | 每个run/case的原始实测数据 |
| summary.json | 原基准质量指标或各分段均值/nearest-rank P50/P95 |

输入与源码运行后再次核对指纹；缺少/重复样本、错误退出或格式错误会把清单标为failed，并保留原始日志。样本必须覆盖每个case在每一遍run中的唯一组合，不只检查总行数。

## 7. 冷启动、RSS与CPU时间的边界

每条命令创建新原生进程，但没有清空文件页缓存；输入hash本身也会读取模型文件。因此load_ms是新进程模型/索引装载，不能写成“冷磁盘加载耗时”。

RSS取GNU time或BusyBox time记录的原生C++命令峰值，单位为KiB，包括装载、质量评测、预热、采样与输出期间的内存，不是单次推理分配量，也不是完整语音助手或NPU内存。Linux资源高水位可跨exec保留，直接从Python父进程启动后读wait4可能混入启动器继承的高水位；本工具通过独立time进程独立启动并测量原生命令。[Linux getrusage说明](https://man7.org/linux/man-pages/man2/getrusage.2.html)、[GNU time内存指标](https://www.gnu.org/software/time/manual/html_node/Memory-Resources.html)

CPU user/system秒数来自所记录的time实现，格式精度0.01秒；显示0.00可能表示小于该精度，不能解读为未执行。wall_seconds还包含进程启动与日志输出。这里未采集CPU/NPU各自执行占比、峰值队列驻留时延或算子内存。

## 8. 如何解释与继续

先保持模型、索引、查询、配置、线程、构建和采样口径一致，再比较结果。小知识库中的Dense/BGE阶段占比不能直接推广到大规模索引；每列的P50也不能相加得到总P50。

当前阶段交付的是观测与基线，不是性能优化收益。后续在RK3588复用相同输入/清单采样，再接入ORT算子trace、perf和驱动侧观测。真实声卡、RKLLM/NPU和TSan仍需分别验证。

## 9. RK3588手动操作与部署清单

板端步骤、实测结果、性能流程图和失败诊断见[RK3588手动性能测试](rk3588-performance.md)。记录器新增 `--build-manifest build-manifest.json`，接受version=1/type=edge_cross_build的交叉构建来源，核对原生命令hash；板端只需二进制/动态库/资产、Python标准库和GNU或BusyBox time，不需要Git、源码树或本机编译器。构建来源与板端实采信息分别保存，不把交叉编译环境写成板端运行环境。
