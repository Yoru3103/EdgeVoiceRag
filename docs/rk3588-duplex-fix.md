# 录放音采样率冲突与环境问句修复

日期：2026-11-10。用户提供的板端日志证明两个独立问题：

```text
[ASR] 现在多少度？
intent=environment_query, use_agent=true
AGENT_STEP action=final_answer（仅返回能力说明，未执行工具）
ASoC: unmatched rate symmetry: fe470000.i2s:16000 - soc_pcm_params_symmetry:44100
failed to apply playback parameters: Invalid argument
```

## 原因与修正

1. 录音保持16k参数用于VAD/ASR与打断采集，TTS源音频为44.1k。同一I2S/codec要求两方向硬件采样率相同；此次驱动明确以symmetry错误拒绝44.1k。这不是DHT11故障，也不是仅凭猜测调整period。
2. 原plughw会选取接近源格式的底层采样率，没有将两路固定到同一个值。新增项目私有config/alsa-rk3588.conf：edge_voice_16k通过plug固定slave.rate=16000，使用内置linear转换器。采集仍16k，TTS仍44.1k源，真正转换PCM后交给16k硬件；不改采样率标签冒充重采样，不关闭打断采集，不改声卡内核驱动。
3. Router包含“多少度”，Rule Planner原来只匹配“温度/湿度”等词。Planner现在复用EnvironmentQuery分类并保留原有温湿度关键词；不更改工具schema、LLM提示词或训练数据。

ASoC约束可对照[Linux6.1源码](https://github.com/torvalds/linux/blob/v6.1/sound/soc/soc-pcm.c)，固定slave.rate与plug转换见[ALSA官方插件说明](https://www.alsa-project.org/alsa-doc/alsa-lib/pcm_plugins.html)。

```mermaid
flowchart LR
    MIC["麦克风 / 底层16k"] --> CAP["plug → 16k采集"]
    CAP --> ASR["VAD / ASR"]
    ASR --> A["Router + Rule Planner"]
    A --> IIO["get_cabin_environment"]
    IIO --> TTS["TTS源PCM 44.1k"]
    TTS --> PLUG["plug实际重采样为16k"]
    PLUG --> OUT["底层16k → 扬声器"]
```

录音和播放通过同一edge_voice_16k配置，硬件两端均16k。ASR/Agent/TTS仍按当前单会话主线运行；图中的双向设备并发由原有会话线程控制。

## 使用小更新包

当前已部署完整包的板端无需再传模型。将更新包放到/userdata：

```sh
cd /userdata
tar -xzf EdgeVoiceRAG-rk3588-20261110-duplex-fix-update.tar.gz
cd EdgeVoiceRAG-duplex-update
# 先退出已有语音/播放进程；脚本不会替你停止它们
sh ./apply.sh /userdata/EdgeVoiceRAG
```

apply.sh先校验更新文件，再将将被替换的旧文件保存到部署根目录新建的backup-duplex-XXXXXX，然后替换程序、配置、启动脚本和清单，最后按完整bundle.sha256核对全部模型/运行库。程序失败时先看日志与备份位置；脚本不启动程序、加载模块或修改系统设备树。conf会恢复到本包基线，原自定义配置在备份中，可核对后迁回，但保持两路device=edge_voice_16k。

没有旧完整目录时使用同版duplex-fix-full.tar.gz，在/userdata解压得到EdgeVoiceRAG。不要把小更新包当成完整模型包。

## 启动前检查

```sh
cd /userdata/EdgeVoiceRAG
. ./env.sh
echo "$ALSA_CONFIG_PATH"
grep -E '^(capture_device|playback_device)=' config/board_rk3588.conf
cat /sys/module/edge_dht11/version
```

预期ALSA_CONFIG_PATH指向本部署config/alsa-rk3588.conf，两路device均为edge_voice_16k，驱动仍为0.2.2-window-qos。本轮DHT11实现没有变化，不要求重载已经正常工作的同版模块。

直接运行二进制时必须先在根目录source env.sh；run.sh与run-audio-check.sh已自动设置ALSA_CONFIG_PATH。此配置只在本进程使用，不覆盖/etc/asound.conf或/root/.asoundrc。Unknown PCM edge_voice_16k表示私有配置未加载。

## 用户板端验收

```sh
cd /userdata/EdgeVoiceRAG
./run-audio-check.sh --mode tts --repeat 2
./run.sh
```

第一项应两次听到测试语音。随后对主程序说“现在多少度？”，预期依次出现get_cabin_environment工具调用、有效temperature_c/humidity_percent、包含读数的播报。不应再出现16000/44100的unmatched rate symmetry。再测一次播放时说话打断，确认没有声音回录造成的误打断；此次修复不等于已完成声学回声消除。

主程序播放时，可在另一个板端终端检查底层实际参数（card编号按/proc/asound/cards，历史为card1）：

```sh
cat /proc/asound/cards
for f in /proc/asound/card1/pcm0c/sub0/hw_params /proc/asound/card1/pcm0p/sub0/hw_params; do
    echo "$f"
    cat "$f"
done
```

在两路参数均已配置的时刻rate应同为16000；播放结束显示closed等状态不算失败，不能用TTS源44100日志代替硬件参数。传感器报错、Unknown PCM、声音不可听或新的ALSA日志分别保存，不混作同一根因。

## 本地验证范围

默认CTest43/43、ALSA-only CTest45/45通过。新增持有16k采集handle时播放44.1k的file/null测试，检查底层文件约16000帧/秒且有非零数据、源音频保持44100；它替代真实物理PCM，不证明板端扬声器已发声。真实Host TTS两次转换/播放通过，工具返回实际环境值的口语路由与IIO刷新回归通过。ARM主程序、Agent与输出探针已编译成功。未远程操作板端，需以上述实际结果完成验收。

## 本次包位置与指纹

小更新包（13371793字节）：`.cache/rk3588-duplex-20261110-modqq7_7/EdgeVoiceRAG-rk3588-20261110-duplex-fix-update.tar.gz`。SHA256：47c724da5167cd66ad6f60bf5981bd1975813fdcb4cfd2db70a24455d1777fbf。

新版完整包（1969939165字节）：`.cache/rk3588-duplex-20261110-modqq7_7/EdgeVoiceRAG-rk3588-20261110-duplex-fix-full.tar.gz`。SHA256：43b00cb916c0753ec464c061238607fda1a829e5e94116bac16c0d58aa8298d0。两者旁边都有.sha256校验文件。

详细本地结果见[实验记录](experiments/2026-11-10-duplex-fix.md)。
