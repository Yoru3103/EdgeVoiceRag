# DHT11失败边沿分析与CPU空闲延迟对照计划

日期：2026-11-09。波形样本来自用户提供的终端日志，本次未连接开发板。

## 失败样本的结论

完整日志含两帧81沿失败记录，以及两帧84沿成功记录。与此前只有计数不同，本次能确认失败帧中出现了不交替的采样电平：

| 帧（板端启动后秒数） | 已记录数量 | 采样电平特征 |
| --- | --- | --- |
| 2754.312 | 81 | 记录1/2、2/3、6/7电平相同；开头持续时间含138.542µs、88.667µs等 |
| 2773.397 | 84 | 可见采样电平交替，解码31.1℃ / 38%RH，offset=2 |
| 2776.431 | 84 | 可见采样电平交替，解码31.9℃ / 38%RH，offset=2 |
| 2780.488 | 81 | 记录1/2、2/3、3/4电平相同，开头采集失真 |

索引指从0开始的边沿记录，不是表中duration所在行的编号。因为日志打印previous_level，连续的previous_level可还原对应相邻记录的电平；不能在这类损坏帧里简单假定每个记录都翻转。

摘录第一帧：

```text
edge=2 duration=88667ns previous_level=1
edge=3 duration=88666ns previous_level=1
edge=4 duration=16625ns previous_level=1
```

这不是仅减少前导记录后仍保持完整40位的证据，直接降低81沿门槛不能恢复该损坏帧。当前保留40位、电平与校验和检查，不修改驱动。

波形后部多为约54µs低电平、24/70µs高电平，提示采集时序问题值得优先排查；但GPIO IRQ日志不是逻辑分析仪，不能证明物理线上没有噪声或电气异常。另需区分：方括号内的时间是采集结束后的打印时刻，实际采样差值是duration字段，不能把打印间隔当作脉宽。

## 可检验假设

假设CPU空闲退出延迟影响早期IRQ采样。使用Linux的/dev/cpu_dma_latency临时注册0µs CPU延迟约束，对比当前同一驱动下的成功率。该接口按打开的文件描述符保持请求，关闭后移除；它不是CPU频率锁定，也不是实时中断延迟保证。[Linux PM QoS官方说明](https://docs.kernel.org/power/pm_qos_interface.html)、[CPU idle官方说明](https://docs.kernel.org/admin-guide/pm/cpuidle.html)

即使结果改善，也只支持空闲状态/系统时序影响的假设，需要重复和物理波形验证；不直接宣布唯一根因为深度休眠。若没有变化，继续调查其他IRQ延迟或电气因素，而不是继续放宽校验。

## 手动实验

脚本[tools/dht11_latency_probe.py](../../tools/dht11_latency_probe.py)只使用Python标准库。默认baseline与qos0各5次，每次读取前等待3秒，避开2秒成功缓存；按IIO name定位传感器，记录每次成功/失败、温度、耗时和两组计数，并读取CPU频率/空闲状态元数据。CPU频率文件仅在采样组前后读取，不做设置。

用户在WSL项目根目录传输：

```bash
ssh root@192.168.10.100 'cat > /tmp/dht11_latency_probe.py' < tools/dht11_latency_probe.py
```

然后在板端关闭明细并运行：

```sh
echo 'module edge_dht11 -p' > /sys/kernel/debug/dynamic_debug/control
python3 /tmp/dht11_latency_probe.py --mode compare --samples 5 --interval 3
```

预计约30～40秒，失败会额外占用驱动超时。保留两条type=summary以及各sample输出。脚本结束/中断时关闭请求句柄，系统移除本进程的临时约束；不会改开机设置、governor或加载模块。若设备节点不可用，脚本报告错误，不创建/dev节点。其他进程的已有QoS请求不会由脚本清除，因此baseline含义是“无本脚本的额外请求”。

脚本完成采样返回0不代表全部样本成功，应读取summary.failure；配置/环境错误返回1，中断返回130。输出是诊断对照，不是正式性能基准或优化收益。

## 本地验证与状态

7项标准库unittest通过：按名称定位、拒绝普通文件、0µs写入与中断关闭、短写关闭、baseline不打开QoS、失败计数不复用旧值、两组顺序与汇总。测试全部模拟QoS接口，不写本机/dev/cpu_dma_latency，也不访问开发板。

板端A/B尚未执行；当前驱动仍为0.2.1-od-value，稳定性未通过。若对照显示明确改善，再评估仅在传感器采集窗口内持有延迟请求，避免将实验约束直接长期常驻。
