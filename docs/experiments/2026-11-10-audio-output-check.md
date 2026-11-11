# 独立语音输出探针：本地验证与待板端验收

日期：2026-11-10。用户已确认真实IIO文字Agent调用正常；本轮推进tone/TTS→ALSA独立输出，板端由使用者执行。没有远程打开板端声卡、录音或运行主程序。

## 改动与定位边界

- 新增src/apps/audio_output_check.cpp；仅EDGE_ENABLE_ALSA=ON生成，tts模式另依赖SHERPA_TTS。
- 复用现有TTS资产/参数、AlsaAudioPlayer，生成一次PCM后在同一播放器上重复提交，先导出无覆盖WAV供aplay对照。
- 播放失败error追加请求device、sample_rate、channels、period_frames_requested；没有更改参数协商、周期/缓冲配置或stop并发行为。
- ALSA null后端测试增加格式切换与stop后再播；CTest增加audio_output_check_smoke。
- 此前完整会话的Invalid argument原因仍未确认；独立输出用于取得可对照的证据。

## 本次实际验证

| 检查 | 实际结果 |
| --- | --- |
| 默认Debug，六个可选后端OFF | 配置/完整构建通过；CTest43/43通过 |
| 新建Host Debug，仅ALSA ON | 完整构建通过；CTest45/45通过 |
| 既有PC可选SDK目录 | 重新配置并构建探针和ALSA测试；聚焦2项CTest通过 |
| CLI/WAV检查 | 13/13通过，包含实际模型TTS到null |
| 独立ARM Release，仅ALSA/TTS ON、BUILD_TESTING OFF | 配置及audio_output_check编译链接通过 |
| 包检查 | ELF及动态依赖均为AArch64；递归DT_NEEDED非系统依赖已收集；bundle.sha256全通过 |
| 板端声音与完整语音回归 | 本轮未执行，待用户实测 |

13项实跑：3种tone采样率、已有WAV保护、6类非法CLI、TTS未编入、错误PCM设备时已导出WAV保留、真实TTS模型。验证WAV采样率/声道/PCM16/帧数，tone非零幅度，正常路径两次play返回ok。全部使用Host null设备，没有访问真实声卡。

现有Host TTS资产一次输出：44100Hz、单声道、PCM16、131072帧，时长2.972154195秒；模型加载2242.231873ms，合成3942.940476ms。它们是Host一次诊断记录，含模型初始化等开销，不是板端性能或正式基准。audio_prepare包含完整分支墙钟时间，不等于load+synthesis简单求和（含析构/其他开销）。

模型SHA256：f085f5079e05f039b800aeb542f5253c26a303211b0c6465d0d9387977855a63；五个资产指纹在包清单。原始Host进程日志/WAV/汇总位于.cache/audio-output-checks-20261110-vfb9u5ii；可追踪摘要见[data/2026-11-10-audio-output-host.json](data/2026-11-10-audio-output-host.json)。

本次未全跑新52项SDK组合、未进行ASan/UBSan重测、没有TSan或板端并发证据。默认与ALSA-only全量、可选SDK聚焦及真实模型探针已覆盖本轮改动；历史51项结果不当成本次执行。

## ARM资产

- tag：audio-output-20261110-xc9gdpdj
- archive：.cache/audio-output-20261110-xc9gdpdj.tar.gz
- bytes：14666167
- archive SHA256：3f17f12de9d07e1c28b9732128a61920b5ab198152b53e24b82a9c83093b845e
- binary SHA256：8d14429157c9875fe3cb16bc403a5dd2ad515893543b29a2ff8ed220c0164e24
- build：.cache/build-rk3588-audio-20261110-cdc5q_bu
- compiler：Buildroot AArch64 GCC13.4，Release，无ASan/UBSan。
- libs：libzmq.so.5、libasound.so.2、libsherpa-onnx-c-api.so、libonnxruntime.so、libstdc++.so.6、libgcc_s.so.1、libbsd.so.0、libmd.so.0。

build-manifest.json保存Git HEAD/dirty状态、cache、编译器、业务源码与运行库指纹，以及未随包提供的TTS资产期望hash。系统glibc/动态加载器和ALSA配置依赖板端；没有将Host同名.so放进ARM包。

Host-only构建目录：.cache/build-host-audio-20261110-dx14mmcu；可选SDK目录：build-board-pc。包只新增独立验证程序，不替换板端已有主程序或模型，解压使用独立新目录。

## 板端验收顺序

按[独立输出手册](../board-audio-output.md)依次测16k/44.1k/48k tone、实际TTS及同WAV的aplay。除程序返回值外，使用者须报告是否真正听到声音。通过后再恢复完整语音，复查录音/VAD/ASR→IIO Agent→TTS/播放与打断；不能由null设备通过推断真实播放通过。
