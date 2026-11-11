#ifndef EDGE_DHT11_FRAME_H
#define EDGE_DHT11_FRAME_H

#include "edge_dht11.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

struct edge_dht11_sample {
    int temperature_milli_c;
    int humidity_milli_percent;
    int offset;
};

/* 纯帧解析，不访问GPIO；失败时不发布任何部分结果。 */
static inline int edge_dht11_decode_at_offset(
    const struct edge_dht11_edge *edges, int count, int offset,
    struct edge_dht11_sample *sample)
{
    unsigned char bytes[EDGE_DHT11_DATA_BYTES] = {0};
    struct edge_dht11_sample candidate;
    int bit;
    unsigned int checksum;

    for (bit = 0; bit < EDGE_DHT11_DATA_BITS; bit++) {
        int rising = offset + bit * 2 + 1;
        int falling = rising + 1;
        edge_dht11_ns_t high_ns;

        if (rising < 0 || falling >= count)
            return -EINVAL;
        if (edges[rising].level != 1 || edges[falling].level != 0)
            return -EIO;
        if (edges[rising].timestamp_ns < 0 ||
            edges[falling].timestamp_ns <= edges[rising].timestamp_ns)
            return -EIO;

        high_ns = edges[falling].timestamp_ns - edges[rising].timestamp_ns;
        bytes[bit / 8] = (bytes[bit / 8] << 1) |
            (high_ns > EDGE_DHT11_ONE_THRESHOLD_NS);
    }

    checksum = (bytes[0] + bytes[1] + bytes[2] + bytes[3]) & 0xff;
    if (checksum != bytes[4])
        return -EBADMSG;

    candidate.humidity_milli_percent = bytes[0] * 1000 + bytes[1] * 100;
    candidate.temperature_milli_c = bytes[2] * 1000 + bytes[3] * 100;
    candidate.offset = offset;
    if (candidate.humidity_milli_percent > 100000 ||
        candidate.temperature_milli_c > 80000)
        return -ERANGE;

    *sample = candidate;
    return 0;
}

static inline int edge_dht11_decode_frame(
    const struct edge_dht11_edge *edges, int count,
    struct edge_dht11_sample *sample)
{
    int offset;
    int ret = -EIO;

    if (!edges || !sample || count > EDGE_DHT11_MAX_EDGES || count < 0)
        return -EINVAL;
    if (count < EDGE_DHT11_MIN_EDGES)
        return -ETIMEDOUT;

    offset = 2 + count - EDGE_DHT11_EXPECTED_EDGES;
    for (; offset >= 0; offset--) {
        ret = edge_dht11_decode_at_offset(edges, count, offset, sample);
        if (!ret)
            return 0;
    }
    return ret;
}

#endif
