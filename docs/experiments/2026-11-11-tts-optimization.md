# 2026-11-11：TTS延迟定位、同Melo FP32与NPU路线

用户板端一次完整温湿度语音返回31.8℃ / 32%RH、播放成功；TTS合成14.511653秒，首播14.536195秒，回答后端24.213291ms，ASR250.64ms。原始日志见[data/2026-11-11-tts-board-before.log](data/2026-11-11-tts-board-before.log)。接收/归档日期为本次工作日期，板端自身日志时钟为1970，不伪造实时时间。

第二轮“讲个笑话”没有完整PERF_JSON。用户报告思考内容播出，按要求暂缓回答内容/思考过滤工作，不将它归因于“没有创意类别”。

## 修改

- BoardAppConfig新增tts_model_file（旧配置缺省model.int8.onnx）及tts_speaker_id（缺省0），新主conf显式选现有Melo model.onnx；保留CPU provider、语速1.0与INT8回退文件。
- makeBoardTtsConfig统一主程序、输出探针与独立基准的资源/线程/speaker映射，不重构TTS执行线程或播放线程。
- tts_benchmark按仓库约定放src/apps，条件为SHERPA_TTS；分离模型构造、首调用/预热/测量、RTF和进程累计RSS。输入FNV与包内SHA256各有明确口径。
- 官方AISHELL3候选下载到独立目录，资源未覆盖Melo；8kHz、30.48MB、speaker66备选，音色/频带需使用者试听。未使用未加载的180MB rule.far，运行仍为date/number FST。
- 安装包保留用户阈值、设备和线程设置，仅更新主conf三项TTS选择；备选conf继承用户主配置并指定4线程。
- 未改ASR provider、LLM Planner/prompt、思考过滤、驱动或设备树；没有启用RKNN或开展新PTQ校准。

## 实际运行的验证

| 检查 | 结果 |
| --- | --- |
| 默认无SDK构建/CTest | 43/43通过 |
| ALSA-only构建/CTest | 45/45通过，含8k/44.1k源到固定16k端转换 |
| PC可选SDK聚焦 | 配置、ALSA、输出冒烟、真实TTS共4/4通过 |
| 模型/说话人/路径配置 | 兼容缺省、映射以及6类非法值通过 |
| 1/2/4/6线程扫描 | 3模型×4线程，每组1预热+3测量，保存原始日志 |
| 最终串行测量 | 3模型，每组1预热+5测量；无并发构建任务 |
| 基准CLI/数据错误 | 8项实际进程检查通过 |
| 两份PCM16 WAV及重复null播放 | Melo FP32与AISHELL3均通过；无实际声卡 |
| 入口目录调整后 | 默认配置/构建/43项、Host/ARM基准目标及输出路径检查通过 |
| ARM目标 | 主程序、输出检查、基准、Agent构建成功；Release全后端、BUILD_TESTING OFF |
| 归档与模拟安装 | 逐文件哈希通过；本地完整旧部署副本备份/更新通过，阈值0.95、普通阈值0.30、max_turns7、自定义TTS线程3均保留 |
| 板端更新后性能/音质 | 未执行，使用者手动复测 |

本轮没有重新全跑52项SDK组合、ASan/UBSan或TSan，未远程操作板端。默认/ALSA全量与实际SDK路径及模型覆盖本次范围。源码入口移动没有改变基准逻辑，已保存实际测量时的原二进制和源文件。

## Host最终5次结果

相同原句、6线程，模型载入与1次预热均不计入summary。数据见[最终摘要](data/2026-11-11-tts-host-final.json)、[线程扫描](data/2026-11-11-tts-host-thread-grid.json)，逐样本JSONL分别保留。

| 模型 | P50ms | P95ms | RTF P50 | 进程峰值RSS KiB |
| --- | --- | --- | --- | --- |
| Melo INT8 | 5702.192273 | 5837.600611 | 1.306239 | 343608 |
| 同Melo FP32 | 1032.643580 | 1039.455608 | 0.237185 | 454816 |
| AISHELL3 FP32、8k | 90.325859 | 103.857464 | 0.018554 | 145440 |

FP32 Melo本机约快5.52倍，进程峰值增加111208KiB。8k备选不能视为同音质替换。样本数只够快速诊断，正式板端对照还需固定环境和更大样本。新SDK使用相同CPU provider；这不是NPU或量化优化成果。

ONNX检查见[图与类型清单](data/2026-11-11-tts-onnx-inspection.json)。INT8图含249个ConvInteger和201个DynamicQuantizeLinear，只说明图实现差异；没有逐算子trace和逐层误差证据，不将其直接判为唯一慢因。也没有仅凭文件名判断数值类型。

首调用记录保留，模型指纹读取可能预热磁盘页缓存，因此model_load_ms不叫冷磁盘时间。VITS输出长度可受随机性影响；每样本RTF使用该样本实际音频时长，P50/P95采用type7线性插值。RSS为整个独立进程累计峰值，不是完整板端语音进程占用。

## 来源与交付

- Melo浮点模型为当前工作区原有资产，FLOAT initializer检查通过；原INT8资产不覆盖，来源/校准过程没有补写。
- 备选来源与所有下载指纹见[候选资产记录](data/2026-11-11-tts-candidate-assets.json)，官方URL与SHA256均保留。
- 基准源/二进制、配置SHA在摘要中；原始日志和WAV目录：/home/xx/EdgeVoiceRAG/.cache/tts-validated-20261111-qjbafz72。
- 更新包：/home/xx/EdgeVoiceRAG/.cache/rk3588-tts-20261111-6co5z1q7/EdgeVoiceRAG-rk3588-20261111-tts-update.tar.gz
- bytes：191444882
- SHA256：3841b3587d9766254176b5bd44288b7170c0f8a0c4468d487f83520d8e1d5d59
- 安装模拟日志：/home/xx/EdgeVoiceRAG/.cache/rk3588-tts-20261111-6co5z1q7/install-test.log

操作见[TTS优化与手动验收](../tts-optimization.md)。用户追加的NPU问题已核对官方支持，SenseVoice RKNN较成熟，当前Melo需另行图适配；详见[迁移路线](../speech-npu-roadmap.md)。
