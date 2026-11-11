/* Host-only semantic tests for the same frame decoder used by the module. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../edge_dht11_frame.h"

static int frame(struct edge_dht11_edge *edges, const unsigned char bytes[5])
{
    int bit, n = 3;
    edge_dht11_ns_t now = 160000;
    edges[0] = (struct edge_dht11_edge){0, 0};
    edges[1] = (struct edge_dht11_edge){80000, 1};
    edges[2] = (struct edge_dht11_edge){now, 0};
    for (bit = 0; bit < 40; bit++) {
        now += 54000;
        edges[n++] = (struct edge_dht11_edge){now, 1};
        now += ((bytes[bit / 8] >> (7 - bit % 8)) & 1) ? 71000 : 25000;
        edges[n++] = (struct edge_dht11_edge){now, 0};
    }
    return n;
}

static void values(const struct edge_dht11_sample *s)
{
    assert(s->temperature_milli_c == 31000);
    assert(s->humidity_milli_percent == 38000);
}

int main(int argc, char **argv)
{
    struct edge_dht11_edge edges[EDGE_DHT11_MAX_EDGES];
    struct edge_dht11_edge shifted[EDGE_DHT11_MAX_EDGES];
    struct edge_dht11_sample result;
    const unsigned char good[5] = {38, 0, 31, 0, 69};
    unsigned char bad[5];
    int n, i, groups = 0;

    n = frame(edges, good);
    assert(n == 83);
    assert(edge_dht11_decode_frame(edges, n, &result) == 0);
    values(&result);
    assert(result.offset == 2);
    groups++;

    /* Full 40 data bits still exist when the first preamble edge is absent. */
    assert(edge_dht11_decode_frame(edges + 1, n - 1, &result) == 0);
    values(&result);
    assert(result.offset == 1);
    groups++;

    shifted[0] = (struct edge_dht11_edge){0, 1};
    for (i = 0; i < n; i++) {
        shifted[i + 1] = edges[i];
        shifted[i + 1].timestamp_ns += 30000;
    }
    assert(edge_dht11_decode_frame(shifted, n + 1, &result) == 0);
    values(&result);
    assert(result.offset == 3);
    groups++;

    for (i = 71; i < EDGE_DHT11_MIN_EDGES; i++) {
        result = (struct edge_dht11_sample){123, 456, 7};
        assert(edge_dht11_decode_frame(edges, i, &result) == -ETIMEDOUT);
        assert(result.temperature_milli_c == 123);
        assert(result.humidity_milli_percent == 456 && result.offset == 7);
    }
    groups++;

    memcpy(bad, good, sizeof bad);
    bad[4] ^= 1;
    n = frame(edges, bad);
    result = (struct edge_dht11_sample){123, 456, 7};
    assert(edge_dht11_decode_frame(edges, n, &result) != 0);
    assert(result.temperature_milli_c == 123);
    assert(result.humidity_milli_percent == 456 && result.offset == 7);
    groups++;

    n = frame(edges, good);
    edges[4].level = 1;
    assert(edge_dht11_decode_frame(edges, n, &result) != 0);
    groups++;

    n = frame(edges, good);
    edges[4].timestamp_ns = edges[3].timestamp_ns;
    assert(edge_dht11_decode_frame(edges, n, &result) != 0);
    edges[3].timestamp_ns = -1;
    assert(edge_dht11_decode_frame(edges, n, &result) != 0);
    groups++;

    {
        const unsigned char humidity_bad[5] = {101, 0, 31, 0, 132};
        const unsigned char temperature_bad[5] = {38, 0, 81, 0, 119};
        n = frame(edges, humidity_bad);
        assert(edge_dht11_decode_frame(edges, n, &result) != 0);
        n = frame(edges, temperature_bad);
        assert(edge_dht11_decode_frame(edges, n, &result) != 0);
    }
    groups++;

    assert(edge_dht11_decode_frame(NULL, 83, &result) == -EINVAL);
    assert(edge_dht11_decode_frame(edges, 83, NULL) == -EINVAL);
    assert(edge_dht11_decode_frame(edges, -1, &result) == -EINVAL);
    assert(edge_dht11_decode_frame(edges, EDGE_DHT11_MAX_EDGES + 1, &result) == -EINVAL);
    groups++;

    /* Replay the user's actual successful IRQ capture, not a second decoder. */
    if (argc != 2) {
        fprintf(stderr, "usage: %s successful-frame.csv\n", argv[0]);
        return 1;
    }
    {
        FILE *file = fopen(argv[1], "r");
        long long ns;
        int level;
        assert(file);
        n = 0;
        while (fscanf(file, "%lld,%d", &ns, &level) == 2) {
            assert(n < EDGE_DHT11_MAX_EDGES);
            edges[n++] = (struct edge_dht11_edge){ns, level};
        }
        assert(feof(file));
        fclose(file);
        assert(n == 83);
        assert(edge_dht11_decode_frame(edges, n, &result) == 0);
        values(&result);
        assert(result.offset == 1);
    }
    groups++;
    printf("DHT11 frame tests: %d case groups passed\n", groups);
    return 0;
}
