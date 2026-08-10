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
#include <linux/timekeeping.h>

#include <linux/iio/iio.h>

#include "edge_dht11.h"

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

// 高位在前,需转换
static u8 edge_dht11_bits_to_byte(const u8 *bits) {
    u8 value = 0;
    int index;

    for (index = 0; index < 8; index++) {
        value <<= 1;
        value |= bits[index] ? 1 : 0;
    }

    return value;
}

/*
 * 低电平起始
 *     ↓
 * 上升沿
 *     ↓
 * 高电平保持26us或70us
 *     ↓
 * 下降沿
 */
static int edge_dht11_decode(
    struct edge_dht11_data *sensor,
    int offset
) {
    u8 bits[EDGE_DHT11_DATA_BITS];
    u8 bytes[EDGE_DHT11_DATA_BYTES];
    int bit_index;
    int byte_index;
    u8 checksum;

    for (bit_index = 0; bit_index < EDGE_DHT11_DATA_BITS; bit_index++) {
        int rising_index = offset + bit_index * 2 + 1;
        int falling_index = offset + bit_index * 2 + 2;

        s64 high_time_ns;

        if (rising_index >= sensor->edge_count || falling_index >= sensor->edge_count) {
            return -EINVAL;
        }

        /*
         * rising_index记录的边沿之后，
         * GPIO应当处于高电平。
         */
        if (!sensor->edges[rising_index].level) {
            dev_dbg(
                sensor->dev,
                "lost synchronization at edge %d\n",
                rising_index
            );

            return -EIO;
        }

        high_time_ns = 
            sensor->edges[falling_index].timestamp_ns
            - sensor->edges[rising_index].timestamp_ns;

        if (high_time_ns <= 0) {
            return -EIO;
        }

        bits[bit_index] = high_time_ns > EDGE_DHT11_ONE_THRESHOLD_NS;
    }

    for (byte_index = 0; byte_index < EDGE_DHT11_DATA_BYTES; byte_index++) {
        bytes[byte_index] = edge_dht11_bits_to_byte(&bits[byte_index * 8]);
    }

    checksum = (bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xFF;

    if (checksum != bytes[4]) {
        dev_dbg(
            sensor->dev,
            "checksum mismatch: calculated=%u received=%u\n",
            checksum,
            bytes[4]
        );

        return -EBADMSG;
    }

    /*
     * DHT11数据格式：
     *
     * bytes[0]：湿度整数
     * bytes[1]：湿度小数
     * bytes[2]：温度整数
     * bytes[3]：温度小数
     */
    sensor->humidity_milli_percent = bytes[0] * 1000 + bytes[1] * 100;
    sensor->temperature_milli_c = bytes[2] * 1000 + bytes[3] * 100;

    if (sensor->humidity_milli_percent < 0 || sensor->humidity_milli_percent > 100000) {
        return -ERANGE;
    }

    if (sensor->temperature_milli_c < 0 || sensor->temperature_milli_c > 80000) {
        return -ERANGE;
    }

    sensor->last_update_ns = ktime_get_boottime_ns();

    dev_dbg(
        sensor->dev,
        "decoded temperature=%d humidity=%d\n",
        sensor->temperature_milli_c,
        sensor->humidity_milli_percent
    );

    return 0;
}

static irqreturn_t edge_dht11_irq_handler(int irq, void *private_data) {
    struct iio_dev *indio_dev = private_data;
    struct edge_dht11_data *sensor = iio_priv(indio_dev);

    int index;
    int level;

    index = READ_ONCE(sensor->edge_count);

    if (index < 0 || index >= EDGE_DHT11_MAX_EDGES) {
        return IRQ_HANDLED;
    }

    /*
     * 该函数在硬中断上下文执行，
     * data_gpio必须是不会sleep的MMIO GPIO。
     */
    level = gpiod_get_value(sensor->data_gpio);

    sensor->edges[index].timestamp_ns = ktime_get_boottime_ns();
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

static int edge_dht11_measure(struct iio_dev * indio_dev) {
    struct edge_dht11_data *sensor = iio_priv(indio_dev);

    long wait_result;
    int ret;
    int offset;
    int start_offset;

    // completion可以重复使用，每次开始新测量前将其重置为未完成状态。
    reinit_completion(&sensor->completion);

    // edgecount会被其他执行上下文同时观察，需要编译器明确每次读写一次
    WRITE_ONCE(sensor->edge_count, 0);

    /*
     * 主机将总线主动拉低18~20ms，
     * 通知DHT11开始发送数据。
     */
    ret = gpiod_direction_output(sensor->data_gpio, 0);

    if (ret) {
        dev_err(
            sensor->dev,
            "failed to drive data GPIO low: %d\n",
            ret
        );

        goto stop_capture;
    }

    usleep_range(
        EDGE_DHT11_START_LOW_MIN_US,
        EDGE_DHT11_START_LOW_MAX_US
    );

    ret = gpiod_direction_input(sensor->data_gpio);

    if (ret) {
        dev_err(
            sensor->dev,
            "failed to release data GPIO: %d\n",
            ret
        );

        goto stop_capture;
    }

    /*
     * 对上升沿和下降沿同时触发中断。
     *
     * 这里每次测量时申请IRQ，测量结束后释放，
     * 避免空闲时的GPIO噪声进入采样数组。
     */
    ret = request_irq(
        sensor->irq,
        edge_dht11_irq_handler,
        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
        dev_name(sensor->dev),
        indio_dev
    );

    if (ret) {
        dev_err(
            sensor->dev,
            "failed to request GPIO IRQ: %d\n",
            ret
        );

        goto stop_capture;
    }

    wait_result = wait_for_completion_killable_timeout(&sensor->completion, msecs_to_jiffies(EDGE_DHT11_TIMEOUT_MS));

    /*
     * free_irq()会等待正在执行的IRQ handler完成，
     * 因此之后可以安全解析edges数组。
     */
    free_irq(
        sensor->irq,
        indio_dev
    );

    if (wait_result < 0) {
        ret = (int)wait_result;

        goto stop_capture;
    }

    if (wait_result == 0 && sensor->edge_count < EDGE_DHT11_EXPECTED_EDGES) {
        dev_err(
            sensor->dev,
            "DHT11 timeout, captured only %d edges\n", sensor->edge_count
        );

        ret = -ETIMEDOUT;
        goto stop_capture;
    }

    edge_dht11_dump_edges(sensor);

    /*
     * 正常情况下edge_count为83，起始offset为2。
     *
     * 某些情况下可能在前面多采集一个边沿，
     * 因此从较大的offset开始逐个尝试，
     * 使用checksum判断是否解码正确。
     */
    start_offset = 2 + sensor->edge_count - EDGE_DHT11_EXPECTED_EDGES;

    ret = -EIO;

    for (offset = start_offset; offset >= 0; offset--) {
        ret = edge_dht11_decode(sensor, offset);

        if (!ret) break;
    }

    if (ret) {
        dev_err(
            sensor->dev,
            "failed to decode DHT11 frame: %d\n",
            ret
        );
    }

stop_capture:
    WRITE_ONCE(sensor->edge_count, -1);

    /*
     * 确保函数返回时GPIO处于输入状态，
     * 不持续拉低DHT11总线。
     */
    gpiod_direction_input(sensor->data_gpio);

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

    sensor->data_gpio = devm_gpiod_get(dev, "data", GPIOD_IN);

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
