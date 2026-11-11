#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/timekeeping.h>

#include <linux/iio/iio.h>

#include "edge_dht11_frame.h"

struct edge_dht11_data {
    struct device *dev;

    struct gpio_desc *data_gpio;

    int irq;

    /*
     * 防止多个用户同时读取IIO节点，
     * 导致两次DHT11通信重叠。
     */
    struct mutex lock;

    struct completion completion;   // 一次性事件通知

    /*
     * edge_count >= 0：正在采集
     * edge_count == -1：没有采集任务
     */
    int edge_count;

    struct edge_dht11_edge edges[
        EDGE_DHT11_MAX_EDGES
    ];

    /*
     * IIO processed值：
     *
     * 温度单位：毫摄氏度
     * 湿度单位：千分之一百分比
     *
     * 例如：
     * 28℃  → 28000
     * 60%  → 60000
     */
    int temperature_milli_c;
    int humidity_milli_percent;

    // 上次成功读取时间，用于缓存。
    s64 last_update_ns;
};

static irqreturn_t edge_dht11_irq_handler(int irq, void *private_data) {
    struct iio_dev *indio_dev = private_data;
    struct edge_dht11_data *sensor = iio_priv(indio_dev);

    int index;
    int level;
    s64 timestamp_ns;

    index = READ_ONCE(sensor->edge_count);

    if (index < 0 || index >= EDGE_DHT11_MAX_EDGES) {
        return IRQ_HANDLED;
    }

    /*
     * 该函数在硬中断上下文执行，
     * data_gpio必须是不会sleep的MMIO GPIO。
     */
    timestamp_ns = ktime_get_boottime_ns();
    level = gpiod_get_value(sensor->data_gpio);
    if (level < 0)
        return IRQ_HANDLED;

    /* IRQ提前就绪后会看到主机释放的上升沿；从传感器响应下降沿开始。 */
    if (index == 0 && level != 0)
        return IRQ_HANDLED;

    sensor->edges[index].timestamp_ns = timestamp_ns;
    sensor->edges[index].level = level;

    index++;
    WRITE_ONCE(sensor->edge_count, index);

    if (index >= EDGE_DHT11_EXPECTED_EDGES) {
        complete(&sensor->completion);
    }

    return IRQ_HANDLED;
}

/*
 * 打印采集到的边沿时间。
 *
 * 使用dev_dbg()，默认不会大量打印。
 * 需要动态调试时可以开启。
 */
static void edge_dht11_dump_edges(struct edge_dht11_data * sensor) {
    int index;

    dev_dbg(
        sensor->dev,
        "captured %d edges\n",
        sensor->edge_count
    );

    for (index = 1; index < sensor->edge_count; index++) {
        s64 duration_ns = sensor->edges[index].timestamp_ns - sensor->edges[index - 1].timestamp_ns;
        
        dev_dbg(
            sensor->dev,
            "edge=%d duration=%lldns previous_level=%d\n",
            index,
            duration_ns,
            sensor->edges[index - 1].level
        );
    }
}

static int edge_dht11_measure(struct iio_dev *indio_dev)
{
    struct edge_dht11_data *sensor = iio_priv(indio_dev);
    struct edge_dht11_sample sample;
    struct pm_qos_request latency_request = {0};
    long wait_result;
    int ret;
    bool irq_requested = false;
    s64 request_start_ns;
    s64 irq_ready_ns;
    s64 release_start_ns = 0;
    s64 release_end_ns = 0;

    reinit_completion(&sensor->completion);
    WRITE_ONCE(sensor->edge_count, -1);

    /*
     * 总线仍空闲时完成IRQ申请/触发配置。注册可能改变GPIO方向，
     * 必须放在起始拉低之前，不能放在18ms拉低过程中。
     */
    /* 仅本次真实测量持有全局CPU延迟约束，避免深空闲退出影响微秒级IRQ采样。 */
    cpu_latency_qos_add_request(&latency_request, 0);

    request_start_ns = ktime_get_boottime_ns();
    ret = request_irq(sensor->irq, edge_dht11_irq_handler,
                      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
                      IRQF_NO_THREAD,
                      dev_name(sensor->dev), indio_dev);
    irq_ready_ns = ktime_get_boottime_ns();
    if (ret) {
        dev_err(sensor->dev, "failed to request GPIO IRQ: %d\n", ret);
        goto stop_capture;
    }
    irq_requested = true;

    /*
     * data_gpio已按开漏配置，使用逻辑电平API切换低/释放。
     * direction_output会先检查IRQ锁，即使开漏也不能这样重设方向。
     * set_value的开漏分支由gpiolib处理输出低/输入高阻的切换。
     */
    gpiod_set_value(sensor->data_gpio, 0);
    ret = gpiod_get_value(sensor->data_gpio);
    if (ret != 0) {
        if (ret > 0)
            ret = -EIO;
        dev_err(sensor->dev, "failed to pull open-drain data GPIO low: %d\n", ret);
        goto stop_capture;
    }
    usleep_range(EDGE_DHT11_START_LOW_MIN_US,
                 EDGE_DHT11_START_LOW_MAX_US);

    /* 起始拉低的IRQ已结束后再开采集，不把主机起始信号当传感器数据。 */
    synchronize_irq(sensor->irq);
    WRITE_ONCE(sensor->edge_count, 0);
    release_start_ns = ktime_get_boottime_ns();
    gpiod_set_value(sensor->data_gpio, 1);
    release_end_ns = ktime_get_boottime_ns();

    wait_result = wait_for_completion_killable_timeout(
        &sensor->completion, msecs_to_jiffies(EDGE_DHT11_TIMEOUT_MS));

    /* 先同步并释放IRQ，再访问完整采样和打印，避免干扰采样。 */
    free_irq(sensor->irq, indio_dev);
    irq_requested = false;
    if (cpu_latency_qos_request_active(&latency_request))
        cpu_latency_qos_remove_request(&latency_request);
    edge_dht11_dump_edges(sensor);
    dev_dbg(sensor->dev,
            "irq_setup_before_start=%lld us, release_call=%lld us\n",
            (irq_ready_ns - request_start_ns) / 1000,
            (release_end_ns - release_start_ns) / 1000);

    if (wait_result < 0) {
        ret = (int)wait_result;
        goto stop_capture;
    }
    if (sensor->edge_count < EDGE_DHT11_MIN_EDGES) {
        dev_err(sensor->dev,
                "DHT11 timeout, captured only %d edges; "
                "irq_setup_before_start=%lld us, release_call=%lld us\n",
                sensor->edge_count,
                (irq_ready_ns - request_start_ns) / 1000,
                (release_end_ns - release_start_ns) / 1000);
        ret = -ETIMEDOUT;
        goto stop_capture;
    }

    /*
     * 82沿也可能包含完整40位：缺少的是前导边沿，而非数据位。
     * 继续逐偏移解码，校验电平、40位内容、校验和与数值范围。
     */
    ret = edge_dht11_decode_frame(sensor->edges, sensor->edge_count, &sample);
    if (ret) {
        dev_err(sensor->dev, "failed to decode DHT11 frame: %d\n", ret);
        goto stop_capture;
    }
    sensor->temperature_milli_c = sample.temperature_milli_c;
    sensor->humidity_milli_percent = sample.humidity_milli_percent;
    sensor->last_update_ns = ktime_get_boottime_ns();
    dev_dbg(sensor->dev,
            "decoded temperature=%d humidity=%d offset=%d\n",
            sample.temperature_milli_c, sample.humidity_milli_percent,
            sample.offset);

stop_capture:
    if (irq_requested)
        free_irq(sensor->irq, indio_dev);
    if (cpu_latency_qos_request_active(&latency_request))
        cpu_latency_qos_remove_request(&latency_request);
    WRITE_ONCE(sensor->edge_count, -1);
    gpiod_set_value(sensor->data_gpio, 1);
    return ret;
}

static int edge_dht11_update(struct iio_dev *indio_dev) {
    struct edge_dht11_data *sensor = iio_priv(indio_dev);

    s64 now_ns = ktime_get_boottime_ns();

    if (sensor->last_update_ns > 0 && now_ns - sensor->last_update_ns < EDGE_DHT11_CACHE_VALID_NS) {
        return 0;
    }

    return edge_dht11_measure(indio_dev);
}

/*
 * IIO读取回调。
 *
 * 用户读取：
 *
 * in_temp_input
 * in_humidityrelative_input
 *
 * 时会进入这里。
 */
static int edge_dht11_read_raw(
    struct iio_dev *indio_dev,
    const struct iio_chan_spec *channel,
    int *value,
    int *value2,
    long mask
) {
    struct edge_dht11_data *sensor = iio_priv(indio_dev);

    int ret;
    (void)value2;

    if (mask != IIO_CHAN_INFO_PROCESSED) {
        return -EINVAL;
    }

    /*
     * IIO sysfs可能被多个线程同时读取，
     * DHT11单总线不能并发通信。
     */
    ret = mutex_lock_interruptible(&sensor->lock);

    if (ret) {
        return ret;
    }

    ret = edge_dht11_update(indio_dev);

    if (ret) goto unlock;

    switch (channel->type) {
        case IIO_TEMP:
            *value = sensor->temperature_milli_c;
            ret = IIO_VAL_INT;
            break;

        case IIO_HUMIDITYRELATIVE:
            *value = sensor->humidity_milli_percent;
            ret = IIO_VAL_INT;
            break;

        default:
            ret = -EINVAL;
            break;
    }

unlock:
    mutex_unlock(&sensor->lock);

    return ret;
}

static const struct iio_info edge_dht11_iio_info = {
    .read_raw = edge_dht11_read_raw,
};

static const struct iio_chan_spec edge_dht11_channels[] = {
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
    },
    {
        .type = IIO_HUMIDITYRELATIVE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
    },
};

static int edge_dht11_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;

    struct iio_dev *indio_dev;
    struct edge_dht11_data *sensor;
    int ret;

    // devm会手动释放，不需要手动kfree
    indio_dev = devm_iio_device_alloc(dev, sizeof(*sensor));

    if (!indio_dev) return -ENOMEM;

    sensor = iio_priv(indio_dev);
    sensor->dev = dev;

    sensor->data_gpio = devm_gpiod_get(dev, "data",
                                      GPIOD_OUT_HIGH_OPEN_DRAIN);

    if (IS_ERR(sensor->data_gpio)) {
        return dev_err_probe(dev, PTR_ERR(sensor->data_gpio), "failed to get data GPIO\n");
    }

    /*
     * IRQ handler中使用gpiod_get_value()，
     * 因此GPIO控制器不能是需要sleep的总线设备。
     *
     * RK3588片上GPIO通常是MMIO GPIO，
     * 正常情况下不会sleep。
     */
    if (gpiod_cansleep(sensor->data_gpio)) {
        dev_err(
            dev,
            "DHT11 GPIO cannot be a sleeping GPIO\n"
        );

        return -EOPNOTSUPP;
    }

    sensor->irq = gpiod_to_irq(sensor->data_gpio);

    if (sensor->irq < 0) {
        return dev_err_probe(
            dev,
            sensor->irq,
            "failed to map GPIO to IRQ\n"
        );
    }

    mutex_init(&sensor->lock);

    init_completion(&sensor->completion);

    sensor->edge_count = -1;
    sensor->last_update_ns = 0;

    indio_dev->name = EDGE_DHT11_DRIVER_NAME;

    indio_dev->info = &edge_dht11_iio_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = edge_dht11_channels;
    indio_dev->num_channels = ARRAY_SIZE(edge_dht11_channels);

    platform_set_drvdata(pdev, indio_dev);

    ret = devm_iio_device_register(dev, indio_dev);

    if (ret) {
        return dev_err_probe(
            dev,
            ret,
            "failed to register IIO device\n"
        );
    }

    dev_info(
        dev,
        "custom DHT11 IIO driver registered, irq=%d\n",
        sensor->irq
    );

    return 0;
}

static const struct of_device_id edge_dht11_of_match[] = {
    { .compatible = "alientek,edge-dht11", },
    { }
};

MODULE_DEVICE_TABLE(of, edge_dht11_of_match);

static struct platform_driver edge_dht11_driver  = {
    .probe = edge_dht11_probe,

    .driver = {
        .name = EDGE_DHT11_DRIVER_NAME,
        .of_match_table = edge_dht11_of_match,
    },
};

module_platform_driver(edge_dht11_driver);

MODULE_AUTHOR("xx");
MODULE_DESCRIPTION("Custom DHT11 temperature and humidity IIO driver");
MODULE_LICENSE("GPL");

MODULE_VERSION("0.2.2-window-qos");
