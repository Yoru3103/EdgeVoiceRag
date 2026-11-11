# 流程图资产与维护

完整说明见[项目调用流程与线程协作图解](../project-flow.md)。SVG可直接用浏览器打开并缩放，无需联网；Markdown阅读器可直接显示图片。

| 图 | 内容 | 绘图函数 |
| --- | --- | --- |
| [01 项目全景](01-project-overview.svg) | 板端、文本、Python三条路线 | `overview` |
| [02 路由与检索](02-routing-and-retrieval.svg) | Agent、安全分类、响应策略和检索 | `routing` |
| [03 线程泳道](03-thread-lanes.svg) | T0～T4、双队列与打断控制 | `threads` |
| [04 正常完成](04-normal-turn.svg) | 分句、合成、播放、End与完成通知 | `normal` |
| [05 起声打断](05-barge-in.svg) | 停播、取消、回收旧任务与复用录音 | `interrupt` |
| [06 Agent工作流](06-agent-workflow.svg) | 规划、工具、跨轮确认与Observation | `agent` |
| [07 进程与通信](07-processes-and-network.svg) | ZeroMQ数据/取消通道与端口 | `network` |
| [08 离线资产](08-offline-assets.svg) | 手册分块、BGE/索引和QLoRA | `offline` |

排版源为[render_flowcharts.py](render_flowcharts.py)，只使用Python标准库。项目根目录运行：

```bash
python3 docs/diagrams/render_flowcharts.py
```

脚本写入本目录13张SVG，输出不含时间戳，可重复生成。修改源码中的布局、文字或连线后重新生成，并同步图解正文。

字体按Microsoft YaHei、PingFang SC、Noto Sans CJK SC等依次回退；预览机器需有可用的中文字体。字号、卡片留白与固定泳道用于保证放大阅读；长方法名与实现边界主要放在正文表格中，避免压进图内。

图中的T0～T4表示应用线程角色，不包含SDK线程池、ZeroMQ内部I/O或内核IRQ。RKLLM回调的实际线程身份尚未通过线程ID实测，修改时保留这一边界。具体条件、错误与竞争分支以正文和当前源码为准。

性能图配套说明见[RK3588手动性能测试](../rk3588-performance.md)。

| 图 | 内容 | 绘图函数 |
| --- | --- | --- |
| [09 板端性能路线](09-rk3588-performance-workflow.svg) | 部署、固定输入、语音验收与结果归档 | `performance_workflow` |
| [10 性能观测边界](10-performance-measurement-points.svg) | 回答、TTS、播放线程与队列等待 | `performance_points` |

DHT11专题见[驱动框图与异常复盘](../dht11-driver-guide.md)。

| 图 | 内容 | 绘图函数 |
| --- | --- | --- |
| [11 驱动分层框图](11-dht11-driver-architecture.svg) | Agent/IIO、缓存、进程、硬中断与硬件依赖 | `dht11_architecture` |
| [12 测量调用时序](12-dht11-measurement-timing.svg) | QoS/IRQ持有、开漏起始、completion与清理 | `dht11_timing` |
| [13 异常与修订](13-dht11-faults-and-fixes.svg) | 漏沿、GPIO接口、帧判定与用户验证结果 | `dht11_faults` |

11～13中的CPU QoS为内核CPU延迟约束，IRQ为硬中断上下文；不套用语音应用的T0～T4线程编号。当前用户反馈已结束首条专项追查，原始实验记录仍保留。
