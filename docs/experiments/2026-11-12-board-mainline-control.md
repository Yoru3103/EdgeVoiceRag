# 2026-11-12：用户TTS认可与主线半双工控制

用户日志于2026-11-11收到，2026-11-12完成本地控制更新与验收准备。包标识沿用开始构建的20261111。板端时钟显示1970，未据此伪造实际运行日期。

用户明确表示当前速度可以、继续主线；随后再次要求思考过滤继续挂起。本轮没有修改思考过滤、LLM提示词或创意回答逻辑，也没有修正短句意图/纯标点ASR。

## 用户提供的实际板端结果

[fast原始日志](data/2026-11-11-fast-board-user.log)有启动模型路径vits-icefall-zh-aishell3/model.onnx、4线程、speaker66，以及两条ok=true的完整PERF_JSON：

| 输入/路径 | ASR ms | TTS累计 ms | 首音频就绪 ms | 首次调用播放 ms | 播放累计 ms |
| --- | --- | --- | --- | --- | --- |
| 现在多少度 / Agent真实IIO | 190.105 | 320.667 | 345.449 | 345.536 | 4483.476 |
| 你是谁 / 固定澄清 | 214.721 | 501.061 | 236.674 | 236.759 | 7181.270 |

第一轮返回30.4℃ / 20%RH；仅是这次读取值，不作为固定测试答案。%仍有OOV警告，读音/单位后续记录。
“首播”这里是调用播放器的应用时刻，不是物理扬声器的声学起始时刻。

默认配置片段按用户标签记录，没有附模型启动段/线程/指纹。“那温度”走RAG/LLM，11句合计TTS44.293s、播放47.430s，并有句子队列14.689s和音频队列4.057s累计push等待；不能当成与fast单句相同任务的TTS对照，也不能把后端计时全算为纯NPU推理。
纯标点“。”走固定澄清，两句合计TTS5.932s，首调用播放2.242s。输入/状态不同，未计算正式fast/default加速比。
capture_elapsed包含长时间等待用户说话，不能直接当作推理时间。

解析后的观测见[JSON记录](data/2026-11-12-board-voice-acceptance.json)。TTS可用性这一阶段按用户认可继续往下走；没有由两条样本推导正式P95、长期稳定性或音质MOS。

## 主线增量

新增barge_in_enabled：
- BoardAppConfig与ContinuousVoiceSessionConfig旧配置/省略字段缺省true，保持原有打断行为。
- 板端参考配置显式false，用于外放半双工基线。
- 关闭时不启动打断录音future；主程序不创建第二套VAD，并保持正常录音、生成/TTS/播放和主动stop。
- 只在future.valid时等待/get/cancel；错误、生成中停止、播放中停止仍能回收回答/工作线程。
- 开启时保留原起声取消、完整新录音复用、request_id隔离和确认session。
- 不改变已有模型/设备/阈值；初始化日志打印Barge-in状态。

## 本轮实际检查

| 检查 | 结果 |
| --- | --- |
| 默认Host完整构建与CTest | 43/43通过 |
| 新开关解析 | 旧conf=true、false禁用、1启用、非法off拒绝 |
| 连续会话生命周期 | 旧12类与半双工4类均通过，CTest条目数未增加 |
| 独立ASan+UBSan聚焦 | board_app_config_tests与continuous_voice_session_lifecycle_tests，2/2通过 |
| 半双工新增行为 | 正常两轮不启barge、回答失败、生成中stop、播放中stop；验证事件/计数/请求ID和线程回收 |
| ARM主程序 | 全后端Release、BUILD_TESTING OFF构建成功 |
| 归档/本地模拟安装 | 校验通过，保持fast模型选择和其他参数；仅缺项时添加false，重复安装保留显式true |
| 新版板端半双工/打断 | 本轮未执行，由使用者手动验收 |

构建/CTest原始日志为.cache/half-duplex-validation-20261111.log。本次没有扩大到全52项SDK、TSan或板端硬件自动测试。

## 交付

- archive：/home/xx/EdgeVoiceRAG/.cache/rk3588-voice-control-20261111-xfaumsub/EdgeVoiceRAG-rk3588-20261111-voice-control-update.tar.gz
- bytes：4567526
- sha256：3dd529d1cedbe4c1b537e2e179d3ad4c36529fb593b4c1062ee8808144388d30
- models_replaced：False
- thinking_filter_changed：False

安装/重复安装模拟日志位于/home/xx/EdgeVoiceRAG/.cache/rk3588-voice-control-20261111-xfaumsub。源、二进制和基底TTS包指纹保存在包内build-manifest.json。

下一步按[固定语音基线与打断/回收验收](../board-mainline-acceptance.md)手动运行，随后推进SenseVoice RKNN独立对照。思考输出继续挂起，不作为这条主线的前置条件。
