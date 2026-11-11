# 2026-11-11 TTS更新

当前TTS首选为原Melo的FP32模型（model.onnx），保留INT8作回退；新增8k中文备选和tts_benchmark。先安装TTS更新包，模型、安装和手动对照见[说明](docs/tts-optimization.md)。完整恢复仍可先解压duplex-fix-full旧基础包，再安装本次TTS更新。主conf保留用户的打断阈值等参数；新的不可变bundle校验与config-runtime.sha256分别记录交付文件和运行配置。

# RK3588完整恢复包

打包日期：2026-11-10。适用于此前使用的ATK-DLRK3588、Linux 6.1.141、AArch64、ES8388声卡部署环境。此包可恢复已删除的EdgeVoiceRAG应用目录；系统镜像、设备树、声卡路由及NPU内核驱动仍使用板端既有环境。

## 包含内容

- board_voice_assistant：当前完整C++主程序；agent_demo、audio_output_check及三个性能工具。
- lib/：递归收集的目标平台非glibc动态库，含sherpa、ORT、RKLLM、ALSA及C++运行库。
- models/：现有DeepSeek RKLLM、SenseVoice INT8 ASR、Melo INT8 TTS、Silero VAD、BGE ONNX/tokenizer；不依赖已删除目录中的模型。
- vector_db/、config/：匹配的手册分块/向量索引与带注释板端配置。
- modules/edge_dht11.ko：从当前修正源码新编译的0.2.2-window-qos；匹配内核6.1.141。
- kernel_modules/edge_dht11/：本次驱动源码和说明，便于追溯。
- build-manifest.json、bundle.sha256：来源、构建、模型、运行库和文件SHA256清单。

默认配置为Hybrid + RKLLM、Rule Planner、IIO环境读取，声卡使用项目edge_voice_16k PCM（物理设备仍为ES8388）。使用现有DeepSeek模型；本地QLoRA adapter没有转换进本包。已针对新日志中的ASoC采样率对称冲突增加项目内plug固定硬件16k配置，并修正“现在多少度”未执行工具的问题；Host与编译验证通过，仍需用户板端复测。

## 1. 解压与校验

将压缩包放到板端有足够空间的目录，例如/userdata；保留压缩包时还需容纳约2.6GB解压内容，以实际du/df为准。包内只有一个顶层目录EdgeVoiceRAG。

以下在板端执行。目录应当已删除或尚不存在；存在时先自行核对，避免覆盖新文件：

```sh
cd /userdata
if [ ! -e /userdata/EdgeVoiceRAG ]; then
    tar -xzf /userdata/EdgeVoiceRAG-rk3588-20261110-duplex-fix-full.tar.gz -C /userdata
else
    echo "EdgeVoiceRAG已存在；先核对，不覆盖"
fi
cd /userdata/EdgeVoiceRAG
sha256sum -c bundle.sha256
. ./env.sh
ldd ./board_voice_assistant
uname -r
```

校验必须通过，ldd不得not found。env.sh设置当前shell的动态库搜索路径和项目私有ALSA_CONFIG_PATH；从目录根运行，模型相对路径才正确。run.sh/run-agent.sh/run-audio-check.sh自动切换工作目录并设置该路径，不向系统安装库，也不加载驱动。

## 2. 加载修正后的DHT11驱动

当前已正常工作的设备树继续使用，无需因删除应用目录再次刷DTB。DATA仍按已确认方案接JP6-3/GPIO4_A5，UART3禁用、JP6跳帽移除、3.3V上拉。GPIO定义来自运行设备树，不是conf。包不修改boot分区、DTB或系统自动加载配置。

```sh
cd /userdata/EdgeVoiceRAG
uname -r
if [ -f /sys/module/edge_dht11/version ]; then
    cat /sys/module/edge_dht11/version
else
    insmod ./modules/edge_dht11.ko
fi
cat /sys/module/edge_dht11/version
```

应为0.2.2-window-qos。删除应用目录不会自动卸载已驻留内核的模块；如果已加载相同版本，保留即可。如果显示旧版本，先退出读取传感器的程序，再手动执行rmmod edge_dht11和insmod ./modules/edge_dht11.ko。只有内核/模块匹配时才能加载，不强制忽略版本或符号错误。

驱动使用开漏value接口拉低/释放，IRQ在起始信号前注册，每次真实采样仅在测量窗口申请CPU延迟QoS并在结束/错误时释放；不需要常驻外部QoS脚本。2秒成功缓存保持原样。

按名称确认IIO设备，编号可能变化：

```sh
for d in /sys/bus/iio/devices/iio:device*; do
    [ -f "$d/name" ] || continue
    echo "$d: $(cat "$d/name")"
done
```

应有name=edge_dht11。重启后手动insmod通常需要再做；包不会自动配置开机加载。

## 3. 先确认独立输出，再恢复完整语音

在板端项目根目录、同一shell中执行。先退出其他语音/播放进程：

```sh
cd /userdata/EdgeVoiceRAG
RUN=$(mktemp -d ./audio-output-XXXXXX)
echo "$RUN"
./run-audio-check.sh --mode tone --sample-rate 16000 --repeat 2 \
  --output-wav "$RUN/tone-16000.wav" > "$RUN/tone.log" 2>&1
echo "tone rc=$?"
cat "$RUN/tone.log"

./run-audio-check.sh --mode tts --repeat 2 \
  --output-wav "$RUN/tts.wav" > "$RUN/tts.log" 2>&1
echo "tts rc=$?"
cat "$RUN/tts.log"
```

tone应听到两次提示音，TTS应听到同一句话两次；日志各有两条audio_check_playback且ok=true。可以新建RUN后补--sample-rate 44100或48000测试音。tts模式不指定采样率；以audio_check_audio中的实际输出为准。现有资产在Host生成44100Hz单声道PCM16，Host结果不替代板端。

只要已有tts.wav，即使程序播放失败仍可对照：

```sh
aplay -D edge_voice_16k -v "$RUN/tts.wav"
```

保存日志和实际是否听到。程序返回成功不等于物理发声；同WAV的aplay对照用于区分输入/系统输出与应用播放器路径，不能直接据此断定单一故障原因。

## 4. 启动Agent与主程序

```sh
cd /userdata/EdgeVoiceRAG
# 文字IIO Agent；输入exit退出后再运行下一项
./run-agent.sh
# 默认加载config/board_rk3588.conf，Ctrl+C退出
./run.sh
```

run-agent.sh默认--device iio，使用Rule Planner，不加载LLM或语音模型；空调/LED仍为进程内逻辑状态。完整语音的agent_planner也保持rule。需要运行自选Agent参数时，例如./run-agent.sh --device mock，会使用你提供的参数。

配置变更请修改config/board_rk3588.conf；run.sh也可接受另一个conf路径。max_turns=0为连续运行。不要同时启动多个主程序共用同一RKLLM handle/声卡资源。

## 验证状态

本轮已完成完整Release ARM用户态程序构建、修正驱动重新构建、驱动Host离线回归及包内ELF/动态依赖/资产和归档校验。新增修复：config/alsa-rk3588.conf固定录放音硬件端16k，TTS源44.1k经过内置linear转换；Rule Planner复用环境查询分类。直接运行二进制前请在根目录source env.sh，两路device必须为edge_voice_16k。板端操作由使用者执行，本轮未登录板端、加载模块或测试声音。此前用户确认DHT11后续测试和IIO文字Agent正常；本次采样率冲突的修复效果与完整语音仍待手动验收。
