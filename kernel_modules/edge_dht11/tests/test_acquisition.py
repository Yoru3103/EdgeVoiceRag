"""Host contract tests executing the driver's real IRQ/measure functions.

Mocks model GPIO/IRQ ownership and waveform inputs; these tests do not claim
to reproduce Rockchip electrical timing or replace board validation.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parent.parent

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edge_dht11_frame.h"

typedef int64_t s64;
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define IRQF_TRIGGER_RISING 1
#define IRQF_TRIGGER_FALLING 2
#define IRQF_NO_THREAD 4
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define msecs_to_jiffies(x) (x)
struct device { int dummy; };
struct gpio_desc { int level; };
struct mutex { int dummy; };
struct completion { int done; };
struct pm_qos_request { int active; };
struct edge_dht11_data;
struct iio_dev { struct edge_dht11_data *private_data; };
#define iio_priv(x) ((x)->private_data)
static void mock_log(const char *format, ...) { (void)format; }
#define dev_dbg(dev, ...) ((void)(dev), mock_log(__VA_ARGS__))
#define dev_err(dev, ...) ((void)(dev), mock_log(__VA_ARGS__))
static const char *dev_name(struct device *dev) { (void)dev; return "mock"; }
static void reinit_completion(struct completion *c) { c->done=0; }
static void complete(struct completion *c) { c->done=1; }

enum scenario { NORMAL, IRQ_FAIL, DRIVE_FAIL, TRUNCATED, INTERRUPTED, BAD_CHECKSUM };
static enum scenario scenario;
static int owned, freed, blocked;
static int qos_active, qos_added, qos_removed;
static void cpu_latency_qos_add_request(struct pm_qos_request *request, int value)
{
    assert(!request->active && !qos_active && value == 0);
    request->active=qos_active=1;
    qos_added++;
}
static int cpu_latency_qos_request_active(struct pm_qos_request *request)
{ return request->active; }
static void cpu_latency_qos_remove_request(struct pm_qos_request *request)
{
    assert(request->active && qos_active);
    request->active=qos_active=0;
    qos_removed++;
}
static s64 clock_ns;
static irqreturn_t (*callback)(int, void *);
static void *callback_data;
static struct gpio_desc gpio;
static s64 ktime_get_boottime_ns(void) { return clock_ns; }

static int request_irq(int irq, irqreturn_t (*fn)(int,void *),
                       int flags, const char *name, void *data)
{
    (void)irq; (void)flags; (void)name;
    assert(!owned && qos_active);
    if (scenario == IRQ_FAIL) return -EBUSY;
    owned=1;
    callback=fn;
    callback_data=data;
    /* IRQ trigger setup leaves the physical pin as input/released. */
    gpio.level=1;
    return 0;
}
static void free_irq(int irq, void *data)
{
    (void)irq;
    assert(owned && data == callback_data && qos_active);
    owned=0;
    freed++;
}
/* The actual gpiolib direction API rejects active IRQs before checking OD. */
int gpiod_direction_output(struct gpio_desc *pin, int value)
{
    if (owned) { blocked++; return -EIO; }
    pin->level=value;
    return 0;
}
int gpiod_direction_input(struct gpio_desc *pin)
{
    pin->level=1;
    return 0;
}
/* Descriptor was obtained as OD: logical 1 releases, logical 0 pulls low. */
void gpiod_set_value(struct gpio_desc *pin, int value)
{
    pin->level = scenario == DRIVE_FAIL && value == 0 ? 1 : value;
}
static int gpiod_get_value(struct gpio_desc *pin) { return pin->level; }
static void usleep_range(int minimum, int maximum)
{
    assert(gpio.level == 0 && qos_active);
    assert(minimum >= 18000 && maximum >= minimum);
    clock_ns += (s64)minimum * 1000;
}
static void synchronize_irq(int irq) { (void)irq; assert(owned); }
'''

WAIT = r'''
static long wait_for_completion_killable_timeout(struct completion *c,
                                                long timeout)
{
    struct edge_dht11_edge input[EDGE_DHT11_MAX_EDGES];
    unsigned char bytes[5]={38,0,31,0,69};
    s64 base=clock_ns;
    int i, n;
    assert(owned && gpio.level == 1 && timeout > 0 && qos_active);
    if (scenario == INTERRUPTED) return -EINTR;
    if (scenario == BAD_CHECKSUM) bytes[4] ^= 1;
    n=frame(input, bytes);
    if (scenario == TRUNCATED) n=71;
    for (i=0; i<n; i++) {
        gpio.level=input[i].level;
        clock_ns=base+input[i].timestamp_ns;
        callback(154, callback_data);
    }
    return c->done ? 1 : 0;
}
'''

MAIN = r'''
static void reset(struct edge_dht11_data *sensor, struct device *dev)
{
    memset(sensor,0,sizeof(*sensor));
    sensor->dev=dev;
    sensor->data_gpio=&gpio;
    sensor->irq=154;
    sensor->temperature_milli_c=123;
    sensor->humidity_milli_percent=456;
    sensor->last_update_ns=100;
    assert(!qos_active);
    owned=freed=blocked=0;
    qos_added=qos_removed=0;
    clock_ns=1000000000LL;
    gpio.level=1;
}
static void unchanged(const struct edge_dht11_data *sensor)
{
    assert(!qos_active && qos_added == 1 && qos_removed == 1);
    assert(sensor->temperature_milli_c == 123);
    assert(sensor->humidity_milli_percent == 456);
    assert(sensor->last_update_ns == 100);
}
int main(void)
{
    struct device dev={0};
    struct edge_dht11_data sensor;
    struct iio_dev iio={&sensor};
    int ret;
    scenario=NORMAL;
    reset(&sensor,&dev);

    ret=edge_dht11_measure(&iio);
    assert(ret == 0);
    assert(sensor.temperature_milli_c == 31000);
    assert(sensor.humidity_milli_percent == 38000);
    assert(!owned && freed == 1 && blocked == 0 && gpio.level == 1);

    assert(!qos_active && qos_added == 1 && qos_removed == 1);

    /* Another measurement must reuse the pin/IRQ without leaking ownership. */
    reset(&sensor,&dev);
    assert(edge_dht11_measure(&iio) == 0);
    assert(edge_dht11_measure(&iio) == 0);
    assert(!owned && freed == 2 && blocked == 0 && gpio.level == 1);
    assert(!qos_active && qos_added == 2 && qos_removed == 2);

    reset(&sensor,&dev);
    scenario=IRQ_FAIL;
    assert(edge_dht11_measure(&iio) == -EBUSY);
    assert(!owned && freed == 0 && gpio.level == 1);
    unchanged(&sensor);

    reset(&sensor,&dev);
    scenario=DRIVE_FAIL;
    assert(edge_dht11_measure(&iio) == -EIO);
    assert(!owned && freed == 1 && gpio.level == 1);
    unchanged(&sensor);

    reset(&sensor,&dev);
    scenario=TRUNCATED;
    assert(edge_dht11_measure(&iio) == -ETIMEDOUT);
    assert(!owned && freed == 1 && gpio.level == 1);
    unchanged(&sensor);

    reset(&sensor,&dev);
    scenario=INTERRUPTED;
    assert(edge_dht11_measure(&iio) == -EINTR);
    assert(!owned && freed == 1 && gpio.level == 1);
    unchanged(&sensor);

    reset(&sensor,&dev);
    scenario=BAD_CHECKSUM;
    assert(edge_dht11_measure(&iio) != 0);
    assert(!owned && freed == 1 && gpio.level == 1);
    unchanged(&sensor);

    reset(&sensor,&dev);
    scenario=NORMAL;
    assert(edge_dht11_measure(&iio) == 0);
    /* Cached reads must not add a new global request. */
    assert(edge_dht11_update(&iio) == 0);
    assert(!qos_active && qos_added == 1 && qos_removed == 1 && freed == 1);
    /* Expiry must acquire/release a new request for the next real frame. */
    clock_ns += EDGE_DHT11_CACHE_VALID_NS + 1;
    assert(edge_dht11_update(&iio) == 0);
    assert(!qos_active && qos_added == 2 && qos_removed == 2 && freed == 2);

    puts("DHT11 acquisition: 9 scenario groups passed");
    return 0;
}
'''


def definition(text, marker):
    """Extract the real named C definition; nested initializers are preserved."""
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                if text[end:end + 1] == ";":
                    end += 1
                return text[start:end] + "\n"
    raise ValueError("unclosed C definition: " + marker)


def run_harness(driver=None, sanitized=False):
    driver = Path(driver) if driver else MODULE / "edge_dht11.c"
    source = driver.read_text()
    fixture = (MODULE / "tests/frame_tests.c").read_text()
    code = PRELUDE + definition(source, "struct edge_dht11_data {")
    code += definition(source, "static irqreturn_t edge_dht11_irq_handler(")
    code += definition(source, "static void edge_dht11_dump_edges(")
    code += definition(fixture, "static int frame(")
    code += WAIT + definition(source, "static int edge_dht11_measure(") + definition(source, "static int edge_dht11_update(") + MAIN
    with tempfile.TemporaryDirectory(prefix="edge-dht11-host-") as directory:
        root = Path(directory)
        c_file, binary = root / "acquisition.c", root / "acquisition"
        c_file.write_text(code)
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitized else []
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-g",
                        *flags, "-I", str(MODULE), str(c_file), "-o", str(binary)],
                       check=True, text=True, capture_output=True)
        args = [str(binary)]
        return subprocess.run(args, text=True, capture_output=True, check=True,
                              env=os.environ.copy())


class AcquisitionTests(unittest.TestCase):
    def test_acquisition_and_cleanup(self):
        self.assertIn("9 scenario groups passed", run_harness().stdout)

    def test_acquisition_with_sanitizers(self):
        self.assertIn("9 scenario groups passed", run_harness(sanitized=True).stdout)


if __name__ == "__main__":
    unittest.main()
