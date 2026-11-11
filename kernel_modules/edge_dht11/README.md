# edge_dht11 内核模块

项目自定义的DHT11 IIO驱动，独立于用户态CMake。接线、设备树及加载步骤见[硬件文档](../../docs/hardware-dht11.md)，当前测量窗口QoS修订见[实验记录](../../docs/experiments/2026-11-09-dht11-qos-window.md)。

- edge_dht11.c：GPIO/IRQ资源、采集、IIO与成功缓存。
- edge_dht11.h：常量、时间戳与边沿结构。
- edge_dht11_frame.h：不访问硬件的共享帧解析器，完整校验成功后才发布结果。
- tests/frame_tests.c：Host语义用例，包含82/83沿、截断、校验和、电平、边界和实际采样回放。
- tests/successful-frame-20261109.csv：使用者首次成功日志转换的83个相对时间戳/电平，无第二份解码算法。

在WSL项目根目录运行离线检查：

```bash
mkdir -p .cache/dht11-tests
cc -std=c11 -Wall -Wextra -Werror -g \
  kernel_modules/edge_dht11/tests/frame_tests.c \
  -o .cache/dht11-tests/frame-tests
.cache/dht11-tests/frame-tests \
  kernel_modules/edge_dht11/tests/successful-frame-20261109.csv

cc -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  kernel_modules/edge_dht11/tests/frame_tests.c \
  -o .cache/dht11-tests/frame-tests-san
.cache/dht11-tests/frame-tests-san \
  kernel_modules/edge_dht11/tests/successful-frame-20261109.csv
```

两次均应输出10 case groups passed。这些用例不属于用户态CTest；ASan/UBSan用于Host解析器，不用于给板端内核模块添加用户态Sanitizer参数。

内核模块按Makefile指定的匹配SDK内核树构建：

```bash
make -C kernel_modules/edge_dht11 ARCH=arm64
```

KERNEL_DIR和CROSS_COMPILE可通过make参数覆盖。异地独立构建时同时复制Makefile、edge_dht11.c和两个头文件，不能遗漏新增的edge_dht11_frame.h。

版本0.2.2-window-qos在真实测量时添加临时CPU延迟约束、结束/失败时释放，缓存命中不添加；在起始信号之前注册IRQ，DATA按开漏配置，运行时用gpiod_set_value拉低/释放，空闲保持释放；电气侧仍要求3.3V上拉。板端检查/sys/module/edge_dht11/version确认实际加载身份。完整帧通常83沿，82沿仅进入严格解码尝试，不代表自动接受缺损数据。

## GPIO/IRQ采集流程回归

```bash
python3 kernel_modules/edge_dht11/tests/test_acquisition.py -v
```

2项标准库unittest分别编译普通和ASan/UBSan版本，每个运行9类采集/清理/缓存场景。测试使用真实驱动函数、共享解析器和GPIO/IRQ模拟，能重现0.2.0错误方向调用，但不验证probe、真实GPIO控制器和IRQ延迟。需Host C编译器与Sanitizer运行库，不访问开发板。

硬件进展：0.2.1已获用户4/10独立温度读取结果，失败明细中出现连续相同电平采样；[下一步延迟对照](../../docs/experiments/2026-11-09-dht11-failed-edges.md)不更换驱动。

新增QoS回归检查：IRQ/采样期间请求有效，各返回路径添加/移除配平，坏帧不发布缓存，缓存命中不新增请求、过期重新申请。SDK需提供Linux CPU latency QoS API；CONFIG_CPU_IDLE关闭时内核为无操作实现。

2026-11-10用户验证：0.2.2无外部QoS下I/O为10/10，但首条温度0.4℃存疑；[数据正确性待核对](../../docs/experiments/2026-11-10-dht11-window-qos-10read.md)，未将读取成功直接算作测量完全正确。

用户后续反馈测试正常，首条专项追查已按要求结束。新增[驱动分层、测量时序与异常复盘](../../docs/dht11-driver-guide.md)，配套3张SVG可离线缩放查看。
