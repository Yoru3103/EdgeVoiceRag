# Host观测与固定输入检索基线：2026-11-08

## 完成内容与环境

本轮接入请求级队列容量等待、共享深度峰值和完整性标记；PERF_JSON沿用v1并兼容旧日志。BGE/Dense增加显式应用层分段采样；既有基准新增可选原始样本输出，离线记录器保存输入/源码/二进制/动态库指纹与原生命令资源数据。具体定义见[性能观测指南](../performance-guide.md)。

| 项目 | 本次实际值 |
| --- | --- |
| Host | Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39 |
| CPU | AMD Ryzen AI 9 HX 370 w/ Radeon 890M |
| 编译器 | c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| 构建 | build-board-pc，Release，ASan/UBSan OFF，BGE ON |
| 推理 | ONNX Runtime CPU，intra_op=4，inter_op=1 |
| 索引 | 8行 × 512维，内存精确搜索 |
| 查询 | 固定29条：24正例、5负例；不是heldout |
| 采样 | 每例30遍，每组870条，四组串行运行 |
| 预热 | benchmark先质量评测1遍、预热1遍；profile预热1遍 |
| 过滤/Top-K | benchmark过滤ON、top_k=3；profile无过滤、top_k=3 |
| 过滤/融合常量 | 稀疏6.0、稠密0.40、候选20；Hybrid RRF k=60、权重1/1 |
| 数据/代码身份 | 四组源码指纹相同；摘要 `8b232f2a7628dcb62a9f056ee54feaf93e2b7be06b9ef3ac42ebc6f3ccba7f56` |
| Git | HEAD `b1a9b13c1f4c399302b406269fa620375e203691`，含未提交工作树修改；HEAD不足以单独复现 |

每条命令启动新进程；没有清空页缓存，输入hash也会读取文件，所以不是冷磁盘实验。未固定CPU频率或单核亲和性，具体CPU允许集合保存在manifest。原始数据目录为 `perf_logs/2026-11-08-m2-baseline30-d4e6a62e`，被Git忽略；新克隆需自行准备资产并重新采样。

## 正式端到端检索基线

以下为同一批固定输入的查询耗时，单位已由工具输出us换算为ms，分位数使用nearest-rank。RSS为GNU time测得的整个原生命令峰值，包括装载、质量评测、预热、采样与输出。
| 后端 | 样本数 | P50 / ms | P95 / ms | 峰值RSS / KiB |
| --- | --- | --- | --- | --- |
| bm25 | 870 | 0.001062 | 0.001733 | 9612 |
| dense | 870 | 3.391409 | 4.280378 | 167212 |
| hybrid | 870 | 3.440451 | 4.294478 | 166960 |

三种后端在这24条正例上的Top1/Recall@3/MRR均为1.0，5条负例拒答率均为1.0。这批用例用于当前项目开发，不能当作泛化准确率、heldout结论或量化后的精度收益。BM25当前知识库很小，不据此推断大规模索引性能。

本次建立的是基线，没有执行优化前后对照，表中差异不是本轮优化收益。各后端来自独立顺序运行，微小差异包含系统调度和缓存波动。

## Dense应用分段

以下为独立profile运行的870条样本。profile无相关性过滤，目的是定位BGE与索引开销，不能等同于过滤后Hybrid整链时间。各列P50/P95不能相加得到总分位数。
| 分段 | P50 / ms | P95 / ms |
| --- | --- | --- |
| lock_wait_ms | 0.000050 | 0.000181 |
| prepare_ms | 0.080933 | 0.134975 |
| inference_ms | 3.255476 | 4.215685 |
| postprocess_ms | 0.005110 | 0.009397 |
| embedding_total_ms | 3.357090 | 4.315674 |
| index_search_ms | 0.004378 | 0.006542 |
| mapping_ms | 0.002906 | 0.004458 |
| total_ms | 3.369492 | 4.327906 |

在当前8行索引和这些输入下，ORT Run远大于索引搜索；这说明后续应先分析实际推理阶段，再决定是否优化索引。lock_wait来自单请求顺序运行，不是并发竞争实验，其极小数值包含计时/获取锁开销。inference_ms是整个Run调用，尚未归因到具体算子或CPU/NPU占比。

## 资源采样校准

试采集曾直接读取从Python启动的子进程rusage，BM25峰值约21MiB；独立GNU time核对约9MiB，发现启动器继承的RSS高水位影响。正式记录器改用GNU time独立测量原生命令，保留resource.tsv；最终表只使用修正后的30遍正式基线。

试采集与5遍快速检查均保留在perf_logs中，并标记不作为本表正式结果。Linux资源指标可跨exec保留；指标语义参考[Linux getrusage](https://man7.org/linux/man-pages/man2/getrusage.2.html)和[GNU time](https://www.gnu.org/software/time/manual/html_node/Memory-Resources.html)。

GNU time CPU秒数精度0.01s，0.00不等于没有CPU执行。多线程累计user时间可能大于墙钟时间。RSS不是单次推理分配量，也不是完整语音程序或NPU内存。

## 输入身份与原始证据

核心输入SHA256：
- `chunks.json`：`3da4298ce119a2334c7a88eb378a76f3a40002f36e1fa5685e4797e4e5c71a3c`
- `retrieval_cases.json`：`17dcd87b08b4733709024a24789d864175677286817b2ef942d9f78b1dcaaf7a`
- `model.onnx`：`4c7d21b79b3201f95e894cca56b6633be3095faa309c9b61fb77f2eb1f679299`
- `tokenizer.json`：`258135af5b19e1c7f25ea95432e699e275053b381b04c5915054467cca8abab3`
- `index_meta.json`：`edaa3b304a494281330c2f9c003db3fc38e375f7406a4afb3ae2e394e97e99d6`
- `embeddings.f32`：`b71440ec6d06ccac7a3d5630c8000d3ff31c671a313df1a8c37f690be0841a90`

各组manifest还保存实际可执行文件、CMake缓存、所需动态库和全部相关源码指纹，并在运行后复核输入/源码未变化。manifest只记录和冻结身份，不声称替代模型/索引全部兼容性校验。

- [Hybrid完整清单](../../perf_logs/2026-11-08-m2-baseline30-d4e6a62e/hybrid-benchmark/manifest.json)
- [Hybrid原始样本](../../perf_logs/2026-11-08-m2-baseline30-d4e6a62e/hybrid-benchmark/samples.jsonl)
- [Hybrid汇总](../../perf_logs/2026-11-08-m2-baseline30-d4e6a62e/hybrid-benchmark/summary.json)
- [Dense分段汇总](../../perf_logs/2026-11-08-m2-baseline30-d4e6a62e/dense-profile/summary.json)
- [Dense分段原始样本](../../perf_logs/2026-11-08-m2-baseline30-d4e6a62e/dense-profile/samples.jsonl)

上述是本机已生成文件；归档实验时应一并保存它们及对应源码/构建版本。复现命令见[性能指南](../performance-guide.md#6-一条命令保存完整基线)，将runs设为30、输出换成新目录即可。

## 实际验证与边界

| 检查 | 本轮结果 |
| --- | --- |
| 默认Debug完整CTest | 43/43通过 |
| ASan/UBSan完整CTest | 43/43通过 |
| PC可选SDK完整CTest | 51/51通过 |
| 离线记录器标准库测试 | 7/7通过 |
| BGE采样前后向量、Dense排序/分数一致 | 对应真实模型测试通过 |
| 旧/新PERF_JSON、真实0等待与部分快照 | 对应报告/汇总测试通过 |
| 原始样本覆盖性 | 四组各870条，run/case唯一且完整，指纹复核通过 |

队列观测功能在Mock/协议测试中验证；本次没有真实语音会话的队列性能分布。未采ORT算子trace、perf调用栈、消费者空等、任务驻留时间、真实RKLLM/NPU/声卡/IIO或TSan数据。

下一步在RK3588使用相同输入/清单采样，收集实际语音PERF_JSON，再对推理热点、线程与资源进行可归因分析。本轮Host结果不替代板端验收。
