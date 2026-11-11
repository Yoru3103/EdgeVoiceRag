# DHT11 接线与板端检查

先看[驱动框图、调用时序与异常复盘](dht11-driver-guide.md)，再按本文接线和加载。

当前采集版本为 **0.2.2-window-qos**，见末尾“测量窗口QoS”及[实测与修订记录](experiments/2026-11-09-dht11-qos-window.md)。前面的旧诊断日志保留为排查历史。

## 当前选择：JP6第3脚 / GPIO4_A5

2026-11-08依据用户提供的《ATK-DLRK3588B V1.1(底板原理图)》完整核对后，DATA改用 **JP6第3脚，GPIO4_A5，3.3V IO**。**先拔下JP6的两个串口选择跳帽，并在设备树禁用UART3**；该接口随后不能同时用于UART3/RS232/RS485。Type-C调试串口使用UART2，不是这里释放的UART3。

原理图第2页将UART3_TX_M2对应到GPIO4_A5；第7页将该信号接至JP6-3。电压依据[厂家核心板引脚表](https://wiki.alientek.com/docs/Boards/Linux/DLRK3588/DLRK3588%20%E6%A0%B8%E5%BF%83%E6%9D%BF%E5%BC%95%E8%84%9A%E5%A4%8D%E7%94%A8%E8%A1%A8/table/)的GPIO4_A5行（3.3V）。

JP6按原理图编号如下；实物以1脚标记和丝印辨认，不按图片左右方向猜：

| 奇数列 | 偶数列 |
| --- | --- |
| 1：RS485_RX | 2：RS485_TX |
| **3：UART3_TX_M2 / GPIO4_A5 → DHT11 DATA** | 4：UART3_RX_M2 / GPIO4_A6 |
| 5：U3_RX | 6：U3_TX |

拔掉跳帽是为了断开RS232/RS485收发器；不能把DATA接到DB9或RS485 A/B端子。VCC与GND从适合的电源接口取得，JP6不是电源接口。

| DHT11连接 | 当前方案 |
| --- | --- |
| DATA | JP6-3 / GPIO4_A5，3.3V逻辑；DTB更新生效后使用 |
| GND | 与板端GND共地 |
| VCC | 按传感器模块额定规格供电；直接连接方案要求3.3V兼容，5V模块需确认电平转换 |
| 上拉 | 若模块未自带合适上拉，在DATA与3.3V之间接约5.1kΩ；不是串联在DATA线上 |

GPIO4_A5默认标记为下拉类型，新的DHT11 pinctrl设置为内部上拉；内部上拉不能替代外部上拉的电气核对。传感器模块若自带上拉到5V，不能直接与本3.3V GPIO相连。上拉建议见[奥松DHT11手册](https://electronics-diy.com/download.php?pdf=DHT11.pdf)第3页。

## 为什么撤回JP5-10的旧选择

先前仅依据JP5局部图选择GPIO0_C6，遗漏了同名网络上的5G复位支路，这个推荐已纠正。

完整V1.1底板图显示：

- 第2页：GPIO0_C6与5G_RESET是同一条信号。
- 第4页：5G_RESET经R40（1kΩ）接Q3（S8050）基极，基极经R41（100kΩ）到GND，发射极接GND。
- 第7页：同一GPIO0_C6引出到JP5-10。

Q3基极支路会形成非线性下拉负载；即使没有插5G模块，该底板支路仍存在。因此不继续在JP5-10上通过简单加上拉尝试修复。该负载是已确认的电路事实，但42/53边沿超时尚未用示波器实测归因。

其他JP5的3.3V引脚也有实际用途：2/4脚是I2C5，连接RTC、Type-C等；6/8脚是I2C6，连接Type-C/触摸屏及其他支路。当前SDK已启用这两路I2C，不为DHT11占用。JP5其余多为1.8V或ADC，也不能直接按3.3V DATA使用。

## 软件配置与证据

当前SDK已将DHT11两处GPIO定义改为GPIO4_A5，并将UART3设为disabled；这里只修改设备树，不修改驱动C代码或C++配置。**板端DTB尚未更新，不能仅移动DATA线就认为新引脚已生效。**

本机SDK文件 `/home/xx/rk3588_linux_sdk/kernel-6.1/arch/arm64/boot/dts/rockchip/rk3588-alientek.dtsi` 第324行附近：

```dts
edge_dht11: humidity-sensor {
    compatible = "alientek,edge-dht11";
    data-gpios = <&gpio4 RK_PA5 GPIO_ACTIVE_HIGH>;
    pinctrl-names = "default";
    pinctrl-0 = <&edge_dht11_data>;
    status = "okay";
};
/* pinctrl 中：
 * rockchip,pins = <4 RK_PA5 RK_FUNC_GPIO &pcfg_pull_up>;
 */
```

同时禁用UART3以释放引脚：

```dts
&uart3 {
    status = "disabled";
};
```

驱动见[edge_dht11.c](../kernel_modules/edge_dht11/edge_dht11.c)，通过 `data-gpios` 获取引脚，不在C++配置文件里指定GPIO。板端配置中的 `agent_iio_device_name=edge_dht11` 是IIO设备名称。

2026-11-08此前检查只发现 `fec10000.saradc`，未发现 `edge_dht11`。SDK源文件里写了节点，不代表当前启动的DTB已包含它，也不代表模块已经加载。

以下由使用者在板端执行，先确认枚举，再读数据：

```sh
for d in /sys/bus/iio/devices/iio:device*; do
    [ -f "$d/name" ] || continue
    echo "$d: $(cat "$d/name")"
done
dmesg | grep -i -E 'edge.?dht11|humidity|gpio'
```

找到名称为 `edge_dht11` 的目录后，读取该目录的 `in_temp_input` 与 `in_humidityrelative_input`。原始值以驱动实现为准，当前驱动导出毫摄氏度和千分之一%RH，C++设备适配层再换算。

若没有节点，依次核对正在启动的DTB、compatible、匹配当前内核构建的驱动模块、probe错误与GPIO占用；先解决枚举，再判断传感器时序/校验和。不要把SARADC的 `iio:device0` 当作DHT11。驱动编译与加载属于独立内核操作，不由用户态CMake自动完成。

## 手动开启驱动：设备树 → 加载模块 → 读取数据

这里使用项目自定义的 `edge_dht11.ko`。开启需要两件事同时成立：启动中的设备树描述了传感器，匹配该内核的模块已加载；C++配置只负责选择读取它。

### 1. 板端先检查

以下均在板端root终端执行：

```sh
uname -r
lsmod | grep edge_dht11
find /userdata/EdgeVoiceRAG/modules /lib/modules -name 'edge_dht11.ko*' 2>/dev/null
```

此前板端内核为6.1.141。本机SDK准备好的内核构建树也报告6.1.141，并有CONFIG_MODULES=y、CONFIG_IIO=y和Module.symvers；版本号一致只是必要检查，仍需使用与板端内核对应的配置和构建产物。

检查实际启动的设备树（不是WSL里的源码）：

```sh
DT=/sys/firmware/devicetree/base/humidity-sensor
if [ -f "$DT/compatible" ]; then
    tr '\000' '\n' < "$DT/compatible"
    [ ! -f "$DT/status" ] || tr '\000' '\n' < "$DT/status"
else
    echo "当前设备树中没有 humidity-sensor 节点"
fi
```

预期compatible包含 `alientek,edge-dht11`，status为okay（省略status也通常视为启用）。如果找不到该路径，可以查找实际节点位置；若整个运行设备树没有相应compatible，单独insmod不能创建设备，需把前述节点编进实际启动的板级DTB并按当前启动布局更新、重启。不要仅修改WSL中的dtsi就认为板端生效，也不要在没有确认启动布局时直接覆盖boot分区。

### 2. 有匹配的ko时加载

假设上一步确实找到了以下文件：

```sh
KO=/userdata/EdgeVoiceRAG/modules/edge_dht11.ko
# 如果modinfo可用，先查看模块信息；没有该命令不影响insmod本身。
command -v modinfo >/dev/null 2>&1 && modinfo "$KO"
insmod "$KO"
lsmod | grep edge_dht11
dmesg | tail -n 60
```

如果已经加载，无需重复insmod。若ko依赖其他模块，需先安装/加载对应依赖；此处已知IIO在本机SDK配置中为内建。加载失败以dmesg为准：Invalid module format常见于内核/架构/vermagic不匹配；Unknown symbol常见于依赖或内核构建不匹配；GPIO busy或probe错误要核对引脚复用和占用。不要强制忽略版本校验。

### 3. 找到IIO节点并读一次

```sh
DHT=
for d in /sys/bus/iio/devices/iio:device*; do
    [ -f "$d/name" ] || continue
    if [ "$(cat "$d/name")" = edge_dht11 ]; then
        DHT="$d"
        break
    fi
done
if [ -n "$DHT" ]; then
    echo "$DHT"
    cat "$DHT/in_temp_input"
    cat "$DHT/in_humidityrelative_input"
else
    echo "尚未注册edge_dht11，检查设备树与dmesg中的probe错误"
fi
```

例如28500表示28.5℃，60000表示60%RH。节点出现只说明设备已注册，能稳定读取才说明传感器通信通过；读超时/校验失败应检查供电、共地、DATA上拉、JP6-3接线、跳帽状态和IRQ时序。当前驱动缓存2秒内的测量，手动重复读时隔至少2秒。

### 4. 板端没有ko时，从WSL编译并复制

本机已有 `kernel_modules/edge_dht11/edge_dht11.ko`，但使用前应确认它对应当前源码与板端内核。需要重新编译时，在WSL运行：

```bash
cd /home/xx/EdgeVoiceRAG/kernel_modules/edge_dht11
make ARCH=arm64 \
  KERNEL_DIR=/home/xx/rk3588_linux_sdk/kernel-6.1 \
  CROSS_COMPILE=/home/xx/rk3588_linux_sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
scp edge_dht11.ko root@192.168.10.100:/tmp/edge_dht11.ko
```

再在板端将KO设为 `/tmp/edge_dht11.ko`，执行第2、3步。若板端SFTP子系统不可用，可用：

```bash
ssh root@192.168.10.100 'cat > /tmp/edge_dht11.ko' < edge_dht11.ko
```

这个临时目标会覆盖同名tmp文件，先确认未被其他用途使用。模块必须针对板端内核构建；WSL自己的内核头文件不适用。外部模块构建要求见[Linux官方Kbuild说明](https://docs.kernel.org/kbuild/modules.html)。

### 5. 最后让应用使用真实设备

在本轮实际使用的conf中设置：

```ini
agent_device_backend=iio
agent_iio_root=/sys/bus/iio/devices
agent_iio_device_name=edge_dht11
```

然后重启应用，问“车内温度是多少”。无需把设备目录编号写死成iio:device0，也不需要为切换设备后端重新编译C++。手动insmod通常仅本次启动有效，重启后需重新加载；先完成手动验证，再按板端Buildroot实际启动机制安排自动加载。

## 本次迁移状态与板端生效顺序

- 用户提供的板端日志已证明旧引脚方案下模块加载、IIO节点注册成功，但测量超时，仅采集42/53个边沿；这不是成功温湿度实测。
- 本机SDK已修改两处GPIO定义（data-gpios和pinctrl），并禁用UART3。原文件备份和校验记录保存在项目的忽略目录 `.cache/dht11-jp6-f94073e6/`。
- 在同一独立目录预处理并编译2mipi720x1280、2mipi800x1280、2mipi1080x1920、2dp四种板级DTB；逐一解析生成的DTB，确认控制器为GPIO4、offset=5、GPIO功能、status=okay以及UART3=disabled。
- DTC存在原有板级警告，已用修改前的dtsi独立编译对照；四种配置的警告内容均与修改前一致。本次未修改SDK的常规DTB/boot镜像构建产物。
- **未更新板端DTB，未重启板端，未在新针脚录取或读取传感器，不能将编译通过写成通信已恢复。**

迁移时按以下顺序由使用者操作：

1. 确认本轮不需要UART3/RS232/RS485，并选用与当前启动板级DTS相同的DTB；四种显示方案不能任意混用。
2. 按板端实际启动方式，将新DTB集成进启动镜像或更新实际加载的DTB，并保留恢复方式。若使用boot.img/FIT打包，不能把裸DTB直接写入boot分区。
3. 断电，拔掉JP6的两个跳帽，将DATA从JP5-10移到JP6-3，确认供电、共地和3.3V上拉/电平兼容。
4. 启动新设备树，再加载原来的匹配内核 `edge_dht11.ko`；引脚来自设备树，本次不需要修改驱动C代码或重新编译用户态应用。
5. 核对GPIO4_A5由DHT11占用且UART3未启用，再按前述方法间隔至少3秒读取。最终是否稳定需由板端日志/波形确认。


## 超时案例：每次只采集到50～51个边沿

2026-11-08使用者在板端提供了三次读取结果：`iio:device1/in_temp_input` 均返回ETIMEDOUT，边沿数分别51、50、50。这说明设备已注册、读取进入驱动并收到部分GPIO中断；不证明电气信号完整或温湿度有效。3秒间隔足够避开本驱动的2秒缓存，增加读取间隔不会补回一帧内漏掉的微秒级边沿。

当前完整帧预期约83个边沿，不能将预期数改成50来“消除超时”。40位数据只在数毫秒内发送，将1秒等待改为更长也不能恢复已丢失的边沿。参考[Linux6.1 DHT驱动](https://github.com/torvalds/linux/blob/v6.1/drivers/iio/humidity/dht11.c)。

先确认短线、共地、JP5-10与外部上拉；特别核对模块DATA上拉电压，不将5V信号接入此IO。同时检查IRQ漏采、可能的强制中断线程化以及释放GPIO后的IRQ注册开销。现有日志不足以确定其中哪一项是根因。使用者已确认：带小板模块、VCC为3.3V、DATA上拉到VCC，并曾在i.MX6ULL项目正常使用。这降低了模块损坏或完全缺少上拉的排查优先级，但不能替代RK3588侧电气波形与IRQ观测。

### 已补充的诊断

驱动在free_irq同步完成后、判断超时前打印边沿明细（dev_dbg），失败也可看到各段电平持续时间；超时错误新增release-to-IRQ-ready微秒值。它测量方向切换调用前至request_irq返回的墙钟间隔，包含方向切换和IRQ注册。IRQ可能在request_irq返回前已处理边沿，因此该值不能解释为整个窗口都没有采集。

上一版诊断模块没有改变83边沿门槛、解码阈值或IRQ申请顺序。代码已在本机匹配SDK内核树交叉编译；随后使用者在板端重载驱动后完成一帧成功读取，早期漏沿根因和连续稳定性仍待验证。

### 使用者手动采集一次诊断

先停止语音助手及其他传感器读取进程。加载根据本次源码新编译的ko后，板端执行：

```sh
[ -f /sys/kernel/debug/dynamic_debug/control ] || mount -t debugfs debugfs /sys/kernel/debug
echo 'module edge_dht11 +p' > /sys/kernel/debug/dynamic_debug/control
cat /proc/cmdline
sleep 3
cat /sys/bus/iio/devices/iio:device1/in_temp_input
dmesg | tail -n 100
echo 'module edge_dht11 -p' > /sys/kernel/debug/dynamic_debug/control
```

IIO编号可能在重新加载后改变，先按name重新枚举。成功/失败都保留本次读取日志。重点看release-to-IRQ-ready、各边沿duration与previous_level：是否有远大于协议正常脉宽的间隙、连续相同电平等；结论需结合实际波形。逻辑分析仪若显示完整40位帧但软件只记录50沿，才更有依据将重点转向IRQ采集；电气波形本身不完整则先解决供电、上拉与连线。

不要在IRQ handler内逐边沿printk；打印会干扰微秒级采样。此处明细统一在采集结束后输出。

## 2026-11-09：已获得一次成功读取

使用者终端返回31000，日志捕获83沿并解码为31℃ / 38%RH，离线复核五字节为[38, 0, 31, 0, 69]，校验和正确。详见[首次成功记录](experiments/2026-11-09-dht11-first-read.md)。一次成功不代表连续稳定；先关闭动态调试，再做10组、间隔3秒的读取。

```sh
echo 'module edge_dht11 -p' > /sys/kernel/debug/dynamic_debug/control
# 此路径对应本次日志；若重新加载后编号变化，先按name定位。
DHT=/sys/bus/iio/devices/iio:device1
for i in 1 2 3 4 5 6 7 8 9 10; do
    echo "sample=$i temperature_mC"
    cat "$DHT/in_temp_input"
    echo "sample=$i humidity_milli_percent"
    cat "$DHT/in_humidityrelative_input"
    sleep 3
done
```

同一组两次读取通常命中同一帧缓存；记录失败次数及错误信息。若再超时，使用诊断模块的release-to-IRQ-ready字段及本次边沿明细继续分析。

## 连续失败后的IRQ修订（0.2.0-irq-first）

后续10组用户采样没有稳定通过，详见[基线与修订记录](experiments/2026-11-09-dht11-irq-fix.md)。新版本将IRQ注册提前到起始信号前，DATA采用开漏方式，支持82沿仍包含完整数据时的严格解码，并提供明确模块版本。

加载后先检查：

```sh
cat /sys/module/edge_dht11/version
```

应为0.2.0-irq-first。新版本超时字段为irq_setup_before_start、release_call；前者已发生在主机起始拉低之前。不要将其等同于旧版release-to-IRQ-ready。

关闭动态调试后，本轮只读温度，每次新的成功测量仍会校验并缓存完整温湿度帧；失败后不立即读取湿度重试：

```sh
echo 'module edge_dht11 -p' > /sys/kernel/debug/dynamic_debug/control
# 重新加载后按name定位，避免IIO编号改变。
DHT=
for d in /sys/bus/iio/devices/iio:device*; do
    [ -f "$d/name" ] || continue
    [ "$(cat "$d/name")" = edge_dht11 ] || continue
    DHT="$d"
    break
done
if [ -z "$DHT" ]; then
    echo "未找到edge_dht11，请检查加载/probe日志"
else
    ok=0
    fail=0
    for i in 1 2 3 4 5 6 7 8 9 10; do
        if value=$(cat "$DHT/in_temp_input"); then
            ok=$((ok + 1))
            echo "sample=$i OK temperature_mC=$value"
        else
            fail=$((fail + 1))
            echo "sample=$i FAIL"
        fi
        sleep 3
    done
    echo "success=$ok failure=$fail"
fi
```

源码及离线验证命令见[模块README](../kernel_modules/edge_dht11/README.md)。当前只有本地编译与解析器测试通过，新版板端成功率尚待使用者验证。

## 修正开漏GPIO操作接口（0.2.1-od-value）

0.2.0-irq-first在用户板端遇到已启用IRQ禁止direction_output的-EIO。原因是上一版用错了方向API，详见[错误与修订记录](experiments/2026-11-09-dht11-od-value.md)。新版通过已配置开漏的gpiod_set_value(0/1)拉低/释放，保留IRQ前置和完整帧校验。

加载后必须核对：

```sh
cat /sys/module/edge_dht11/version
```

应为0.2.1-od-value。先按name找到设备，间隔3秒后只读取一次in_temp_input；确认没有direction_output的-EIO后，再执行前文10次独立温度采样。若仍超时，保存新版irq_setup_before_start、release_call及边沿明细。新版已获用户连续10次采样结果：4成功、6失败，见[本轮记录](experiments/2026-11-09-dht11-4-of-10.md)；稳定性仍未通过。

## 0.2.1复测后：保存失败帧明细

当前结果为4/10成功，方向保护错误消失，但存在80/81沿超时与解码-EIO。继续使用当前模块，先按name枚举DHT，然后执行以下3次采样并保存日志：

```sh
cat /sys/module/edge_dht11/version
DHT=
for d in /sys/bus/iio/devices/iio:device*; do
    [ -f "$d/name" ] || continue
    [ "$(cat "$d/name")" = edge_dht11 ] || continue
    DHT="$d"
    break
done
if [ -z "$DHT" ]; then
    echo "未找到edge_dht11"
else
    echo 'module edge_dht11 +p' > /sys/kernel/debug/dynamic_debug/control
    for i in 1 2 3; do
        sleep 3
        echo "sample=$i"
        cat "$DHT/in_temp_input"
        rc=$?
        echo "sample=$i rc=$rc"
    done
    echo 'module edge_dht11 -p' > /sys/kernel/debug/dynamic_debug/control
    dmesg | grep -E 'edge_dht11|humidity-sensor' | tail -n 350
fi
```

保留完整captured N edges及后续duration/previous_level行，尤其是失败的那一帧。若3次均成功，可按同样方法再采3次寻找失败样本；这不是扩大性能基准，而是获取根因分析所需的失败数据。此前失败明细没有启用，dmesg无法补出当时未记录的边沿。

## 已取得失败帧：下一步做CPU延迟对照

两帧81沿记录都出现相同电平连续采样，说明不能当作“只缺前导”的完整帧接受。具体证据与对照方案见[失败边沿分析](experiments/2026-11-09-dht11-failed-edges.md)。当前不换驱动、不降低门槛。

由使用者手动运行tools/dht11_latency_probe.py，baseline与临时CPU延迟约束各5次。约束通过/dev/cpu_dma_latency的文件描述符保持，脚本退出后释放；只用于排查CPU空闲状态对采集时序的影响，不代表根因已确定。运行步骤和结果字段见上述实验记录。

## 测量窗口QoS（0.2.2-window-qos）

使用者以0.2.1做A/B：普通模式0/5成功，外部0µs CPU延迟请求下5/5成功，成功读取约24.6ms。原始记录与解释见[窗口QoS实验](experiments/2026-11-09-dht11-qos-window.md)。

0.2.2在真实测量时添加CPU延迟请求，在IRQ释放后及所有错误退出时移除；缓存命中不添加。当前已完成本地验证/交叉编译，新版板端仍待验证。

手动加载并确认版本0.2.2-window-qos后，确保旧A/B脚本已退出，用以下命令仅测驱动自身窗口请求：

```sh
python3 /tmp/dht11_latency_probe.py --mode baseline --samples 10 --interval 3
```

这里baseline只表示没有脚本额外约束，不表示新驱动关闭QoS。先记录真实成功率，再验证配对温湿度和Agent路径。不要把外部qos0的5/5当作新版模块已通过。

## 2026-11-10：I/O为10/10，后续复测反馈正常

用户已完成0.2.2在无脚本额外QoS下的10次读取，全部返回成功，并补充后续测试没有问题。按用户要求结束首条专项追查，不再安排重载/首帧采样；原始值保留在[实验记录](experiments/2026-11-10-dht11-window-qos-10read.md)。

当前继续推进真实温湿度→Agent联调。驱动框图和历史异常修订见[驱动图解](dht11-driver-guide.md)。
