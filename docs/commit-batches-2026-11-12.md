# 2026-11-12分步提交说明

本次将此前累积的板端C++主线、验证与记录分步上传。当前分支为feature/agent-sft-qwen25-3b；目录整理提交b1a9b13此前只在本地，本次与后续提交一并正常快进推送。没有改写历史或强制推送。

## 实现/数据提交

| 顺序 | 提交 | 范围 |
| --- | --- | --- |
| 1 | [42c716e2](https://github.com/Yoru3103/EdgeVoiceRag/commit/42c716e2df2472f2f3908d6653dbc0e49789b8e0) | 语音生命周期、关闭打断的核心能力、队列观测和ASan/UBSan诊断构建 |
| 2 | [29eba386](https://github.com/Yoru3103/EdgeVoiceRag/commit/29eba3868339c23898fef6b4a4b4eb077a76ea08) | BGE/Dense分段、固定输入基线、资源/指纹与独立部署工具 |
| 3 | [6277cf55](https://github.com/Yoru3103/EdgeVoiceRag/commit/6277cf55da88a69eaa410b8920a5f2bede0ce2e2) | DHT11开漏value操作、IRQ前置、测量窗口QoS与离线回归 |
| 4 | [9acda6ef](https://github.com/Yoru3103/EdgeVoiceRag/commit/9acda6ef24796ae62f7bb66dc028bdeca1626e12) | 真实IIO文字入口与口语环境查询的Rule工具调用 |
| 5 | [e0c5b051](https://github.com/Yoru3103/EdgeVoiceRag/commit/e0c5b0516899ad1ee835c2a80db126b4f6f67514) | 共享TTS配置、FP32/备选基准、固定16k录放音和应用打断开关 |
| 6 | [b47f1fb0](https://github.com/Yoru3103/EdgeVoiceRag/commit/b47f1fb059bbc8a409aa083aa9b1a143575a14a8) | 此前已有的Planner鲁棒性heldout、同义词分组与分类评测 |


第7步为本说明所在的文档提交，收录模块/API/CMake说明、13张SVG、测试手册、部署步骤和各轮实测记录。共享tests/CMakeLists按依赖顺序分段暂存，工作区文件内容未因拆分而回退；中间提交没有逐个重建，验证针对最终集成树执行。

## 本次上传前实际检查

- 默认Host构建成功，CTest43/43。
- Python标准库诊断/性能工具测试20/20。
- 既有Planner评测两份pytest文件7/7；未训练或重新生成数据。
- 驱动采集普通/Sanitizer测试2/2；帧解析普通与ASan/UBSan各10组通过。
- Markdown本地链接、JSON/JSONL、SVG XML与git diff --check检查。
- 可选语音SDK/ARM、Sanitizer全量、板端实测沿用各实验记录中的实际执行轮次，没有记作本次重新全部执行。

## 当前成果与边界

- 用户已验证DHT11后续读取和IIO Agent调用；fast语音两个样本成功、用户认可速度。它们不是长期稳定性或正式P95。
- 主线现有barge_in_enabled配置，但新半双工控制更新的板端验收仍由使用者操作，Mock/ASan结果不替代硬件证据。
- 空调/LED仍为内存逻辑；已有INT8、RKLLM或模型文件不等于已完成通用RKNN或PTQ/逐层误差分析。
- 思考输出问题按用户要求继续挂起；ZeroMQ示例/独立服务和Python运行时路线不扩展。
- 模型/索引、SDK二进制、模块ko、DTB、压缩部署包、构建目录均不提交。日志只纳入文档引用的两份小原始记录。
- .cache中的历史包与临时密钥/连接信息不入Git；旧DHT11临时构建目录被忽略。新克隆需按文档另行准备资产。

## 下一阶段

用户指定手动学习RKNN操作。下一阶段只编写步骤并只读核对现有源码/官方说明，智能体不编译RKNN SDK、不下载/转换RKNN模型、不改provider或板端环境。
路线为独立官方SenseVoice参考 → CPU/RKNN同输入对照 → 自己交叉编译SDK → 自己导出/转换 → 最后再接回主程序。运行现成模型与自己完成PTQ分开验收。

第8步为[RKNN手动学习手册](sensevoice-rknn-hands-on.md)与导航同步的独立文档提交；未修改运行环境、模型或业务代码。
