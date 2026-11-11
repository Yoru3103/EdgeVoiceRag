# DHT11 CPU延迟A/B结果与测量窗口QoS修订

日期：2026-11-09。A/B数据由使用者在RK3588终端采集；新版模块只完成本地构建/验证，尚未加载板端。

## 用户A/B实测

驱动为0.2.1-od-value，Linux6.1.141，按IIO名称定位到iio:device1。脚本SHA256为452bd3569bc232fe79429c4ae19749772c19463f6713fab047b8e6b8e037955e，与本机tools/dht11_latency_probe.py一致。原始数值转录见[data JSON](data/2026-11-09-dht11-qos-ab.json)，不包含新运行的测量。

| 模式 | 成功 / 总数 | 每次sysfs读取耗时（ms） |
| --- | --- | --- |
| baseline，无本脚本额外QoS | 0 / 5 | 1038.357250、1031.009334、1031.776084、1031.135000、1031.409751 |
| qos0，临时0µs CPU延迟请求 | 5 / 5 | 24.606458、24.696000、24.587208、24.631250、24.863125 |

baseline捕获边沿数为53、35、34、38、34，均超时。qos0返回温度32000、32700、32100、32600、32700，单位毫摄氏度；成功帧的边沿数量没有打印，不能补写为83。

两组governor均为schedutil。cpu0的idle状态显示WFI退出延迟1µs、cpu-sleep退出延迟220µs，均未在sysfs被禁用。QoS通过governor约束可选择状态，不要求修改disable字段。qos0组前后频率快照均为408000；快照不是连续频率trace，不能完全排除帧内升降频影响。

结果支持CPU空闲/系统时序影响IRQ采集的假设；样本小、按先baseline后qos0顺序，不能宣布长期稳定或唯一根因。成功读取约24.6ms包含18～20ms起始拉低、采集与sysfs开销，不是纯数据传输时间。两组一边失败一边成功，不把耗时差写成同等正确性下的性能收益。

## 驱动修订：0.2.2-window-qos

使用[Linux CPU latency QoS API](https://docs.kernel.org/power/pm_qos_interface.html)：

1. 只在真实测量函数中创建局部pm_qos_request，添加0µs约束。
2. 在IRQ注册和起始信号前添加，使18ms起始阶段也处于约束内。
3. IRQ同步释放后立即移除约束，再打印和解析；未成功申请IRQ等提前退出也通过统一清理移除。
4. 正常、截断超时、解码错误、等待被中断、IRQ申请失败、拉低失败均释放本次请求。
5. 2秒有效缓存命中直接返回，不创建新请求。未改CPU governor、用户配置或开机加载方式。

这是全局CPU延迟请求，不是单核频率锁定。正常采集时只覆盖本次测量，失败时可能持续到驱动超时/清理结束；它不提供硬实时保证，也不等于单片机定时捕获。CONFIG_CPU_IDLE关闭时内核提供无操作API，本机SDK已启用CONFIG_CPU_IDLE。

## 本地验证

- 实际采集函数的模拟回归扩为9类：正常、重复测量、IRQ申请失败、拉低失败、截断帧、等待中断、坏校验、缓存命中、缓存过期重新测量。
- 模拟断言请求在IRQ/采样期间有效，返回后添加/移除数量配平，失败不发布缓存，缓存命中不新增请求。
- 2项标准库unittest分别编译普通和ASan/UBSan版本，每版9类全部通过。模拟不执行真实PM QoS内核实现、probe或IRQ时序。
- 同轮共享帧解析器10组用例，普通与ASan/UBSan均通过。
- AArch64模块交叉编译成功，vermagic=6.1.141 SMP mod_unload aarch64，version=0.2.2-window-qos。
- 构建目录：.cache/dht11-qosfix-20261109-2rdfk10h。
- ko SHA256：e11067bf41273eed3782c124df9b57f9eec6c94aefcefcdf196df4241ac45af5。

新版板端成功率尚待使用者验证；没有将外部长期持有QoS的5/5直接算作新版驱动通过。

## 使用者手动加载与验证

WSL项目根目录：

```bash
ssh root@192.168.10.100 'cat > /tmp/edge_dht11-window-qos.ko' \
  < .cache/dht11-qosfix-20261109-2rdfk10h/edge_dht11.ko
```

板端停止其他读取程序，确认外部QoS对照脚本已经退出，再执行：

```sh
rmmod edge_dht11 && insmod /tmp/edge_dht11-window-qos.ko
cat /sys/module/edge_dht11/version
echo 'module edge_dht11 -p' > /sys/kernel/debug/dynamic_debug/control
python3 /tmp/dht11_latency_probe.py --mode baseline --samples 10 --interval 3
```

版本必须为0.2.2-window-qos。此处baseline表示无额外用户态请求，新驱动在每次测量内部仍自行持有窗口请求；不要再用qos0掩盖内部窗口是否有效。记录10次是否均成功和elapsed_ms。若出现失败，保存新版日志继续分析，不把请求常驻作为替代。

如果10次通过，再做温湿度配对读取、空闲/负载切换与更长时间验证，并进入Agent真实传感器联调；DHT11传感器精度和真实语音链路仍需分别验证。

2026-11-10后续：用户在新版baseline模式完成10/10读取，但首条0.4℃与后9条31.1～31.9℃明显不一致；见[后续记录](2026-11-10-dht11-window-qos-10read.md)。I/O通过与数据正确性分开验收。
