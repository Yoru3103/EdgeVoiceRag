#ifndef __EDGE_DHT11_H
#define __EDGE_DHT11_H

#include <linux/types.h>

#define EDGE_DHT11_DRIVER_NAME "edge_dht11"

/*
 * DHT11一次返回40位：
 *
 * 8位湿度整数
 * 8位湿度小数
 * 8位温度整数
 * 8位温度小数
 * 8位校验和
 */
#define EDGE_DHT11_DATA_BITS   40
#define EDGE_DHT11_DATA_BYTES   5

/*
 * DHT11完整通信中大约产生83个有效边沿。
 * 数组稍微留大，便于调试异常脉冲。
 */
#define EDGE_DHT11_EXPECTED_EDGES 83
#define EDGE_DHT11_MAX_EDGES      90

/*
 * 主机起始信号：
 * 将数据线拉低至少18ms，然后释放。
 */
#define EDGE_DHT11_START_LOW_MIN_US 18000
#define EDGE_DHT11_START_LOW_MAX_US 20000

/*
 * DHT11使用高电平持续时间表示0和1：
 *
 * 0：大约26~28us
 * 1：大约70us
 */
#define EDGE_DHT11_ONE_THRESHOLD_NS 50000LL

// 一次读取最长等待时间
#define EDGE_DHT11_TIMEOUT_MS 1000

// 2秒内重复读取直接返回数据
#define EDGE_DHT11_CACHE_VALID_NS 2000000000LL

struct edge_dht11_edge {
    s64 timestamp_ns;
    int level;
};

#endif // !__EDGE_DHT11_H