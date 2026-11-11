# 板端主线：固定语音基线与打断/回收验收

日期：2026-11-11。使用者已确认当前速度可以，fast配置温湿度查询TTS320.667ms、首次调用播放345.536ms，另一个澄清回合首播236.759ms。TTS选型不再作为后续推进的阻塞项；思考输出继续按用户要求挂起，当前不修改相关逻辑。

本轮先让录音→ASR→Agent→TTS/播放的半双工行为可控，再单独开启打断检查线程/请求隔离，之后推进SenseVoice RKNN。板端由使用者手动操作。

## 1. 新增真正的打断开关

```ini
# false：回答期间不监听打断，播报完成后再录音
barge_in_enabled=false
```

- false不创建第二套VAD、不启动barge录音future；正常录音、Agent、TTS、播放及主动stop()仍工作。
- true恢复原有打断监听，继续使用已有barge_vad_threshold；阈值仍须大于0、小于1。
- 旧conf未写此项时缺省true，以保持兼容；当前板端参考conf显式false用于外放基线。
- 关闭打断的模式不是AEC；声音结束后的房间尾音仍需观察。
- 启动应看到Barge-in: disabled或enabled，旧版程序不识别此开关，必须更新二进制。

## 2. 安装小更新包

将voice-control更新包放到/userdata，先退出当前程序：

```sh
cd /userdata
tar -xzf EdgeVoiceRAG-rk3588-20261111-voice-control-update.tar.gz
cd EdgeVoiceRAG-voice-control-update
sh ./apply.sh /userdata/EdgeVoiceRAG
```

更新针对已经安装TTS更新的部署。它不更换模型、驱动、词表或系统库，只更新主程序、清单和说明。安装前校验依赖，备份到新的backup-voice-control-XXXXXX。
现有主conf未设置barge_in_enabled时添加false；已设置则保留用户选择。当前TTS模型、线程/音色、VAD阈值、max_turns和设备参数均保留。
备选fast配置同样仅在缺项时添加false；校验清单把可修改conf单独记录。

## 3. 第一组：半双工自然完成三轮

保持当前已经确认速度的TTS配置。实验使用副本，先不改真实设备行为或LLM提示词：

```sh
cd /userdata/EdgeVoiceRAG
RUN=$(mktemp -d ./voice-baseline-XXXXXX)
awk '
/^[[:space:]]*barge_in_enabled[[:space:]]*=/ {print "barge_in_enabled=false"; b=1; next}
/^[[:space:]]*max_turns[[:space:]]*=/ {print "max_turns=3"; m=1; next}
{print}
END {
 if (!b) print "barge_in_enabled=false"
 if (!m) print "max_turns=3"
}' config/board_rk3588.conf > "$RUN/baseline.conf"

# 保存日志及真实程序退出码；不把tee的退出码当成程序退出码。
(
    if ./run.sh "$RUN/baseline.conf"; then RC=0; else RC=$?; fi
    printf '%s\n' "$RC" > "$RUN/exit-code.txt"
    exit "$RC"
) 2>&1 | tee "$RUN/voice.log"
```

按每次“等待用户说话”提示，依次说：

| 回合 | 固定输入 | 检查结果 |
| --- | --- | --- |
| 1 | 现在多少度？ | get_cabin_environment返回实际IIO读数，播报可听 |
| 2 | 车内湿度是多少？ | 同一工具再次查询；可能命中2秒缓存，不用固定温湿度数值当答案 |
| 3 | 空调状态 | get_air_conditioner_state返回内存逻辑状态，无写操作 |

回答时可说话或播放旁边声音，关闭模式不应出现BARGE-IN，也不应把这段声音当成新问题提前排队。播报结束后才说下一句；重复确认每次“等待用户说话”能正常录入。

自然结束后检查：

```sh
cat "$RUN/exit-code.txt"
./voice_performance_summary "$RUN/voice.log"
```

预期程序退出码0、Completed turns: 3、Interruptions: 0；3轮合法Agent请求均有正常完成与PERF_JSON。模型/线程/说话人和Barge-in启动行一起保存。
capture_elapsed_ms包含等待人开口，比较TTS/首播时不要把等待时间当推理耗时。
此组三轮是控制行为验收，不作为正式P95基准；后续固定模型/配置后再累计每类更大样本。

## 4. 第二组：独立开启打断

第一组通过后，从同一实验配置仅修改开关：

```sh
sed 's/^barge_in_enabled=.*/barge_in_enabled=true/' "$RUN/baseline.conf" > "$RUN/barge.conf"
./run.sh "$RUN/barge.conf"
```

先问“现在多少度？”，在播报中说“车内湿度是多少？”。检查：
1. 起声后出现一次BARGE-IN，旧音频停止；新问题被识别并查询。
2. request_id更新，session_id保持board-voice-session；旧音频/文本不污染新轮。
3. 后续再问“空调状态”，可继续录音和播报，最后按max_turns自然退出。
4. 记录真打断与扬声器回录误触发。外放未接AEC，误触发单列记录，不以调成阈值1伪装禁用。

打断回合计数包含被中断的轮次，所以不能要求每次Completed turns都对应一条成功PERF_JSON。第一组半双工基线与第二组打断数据分开汇总，不混算速度。

## 5. 回收证据与下一步

本轮Host已有16类连续会话生命周期场景：旧12类打断/失败/stop边界，加半双工正常两轮、回答失败、生成中停止、播放中停止4类。断言未启动barge recorder、无误打断事件、request_id隔离、stop取消与工作线程回收；普通与ASan/UBSan聚焦均通过。

板端先观察max_turns自然返回与进程退出，保存异常/残留播放/无法继续录音的日志。Ctrl+C默认终止进程不能单独当作SDK析构和线程正常回收的证明；长期内存/RSS和真实SDK取消仍按实际结果继续积累。

思考过滤、创意回答、“那温度”等短句意图、纯标点识别和%读音问题作为已有记录保留，不在此次开关更新中顺带修改，也不要求先解决才能做主线/NPU工作。
下一阶段按[语音NPU路线](speech-npu-roadmap.md)准备独立SenseVoice CPU/RKNN对照，再接回同一语音主线；当前没有更改ASR provider或系统驱动。

交付于2026-11-12，包标识沿用20261111。归档：`.cache/rk3588-voice-control-20261111-xfaumsub/EdgeVoiceRAG-rk3588-20261111-voice-control-update.tar.gz`，4567526字节（约4.6MB），SHA256：3dd529d1cedbe4c1b537e2e179d3ad4c36529fb593b4c1062ee8808144388d30。

证据与本地检查见[阶段记录](experiments/2026-11-12-board-mainline-control.md)。
