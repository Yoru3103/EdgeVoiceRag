#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "bounded_blocking_queue.h"

namespace {

using namespace std::chrono_literals;

int failed_count = 0;

void expectTrue(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failed_count;
}

void testFifoOrder() {
    BoundedBlockingQueue<int> queue(3);

    expectTrue(
        queue.push(10),
        "push first value"
    );
    expectTrue(
        queue.push(20),
        "push second value"
    );
    expectTrue(
        queue.push(30),
        "push third value"
    );

    const auto first = queue.pop();
    const auto second = queue.pop();
    const auto third = queue.pop();

    expectTrue(
        first.has_value() && *first == 10,
        "pop first value in FIFO order"
    );
    expectTrue(
        second.has_value() && second.value() == 20,
        "pop second value in FIFO order"
    );
    expectTrue(
        third.has_value() && third.value() == 30,
        "pop third value in FIFO order"
    );
}

void testProducerBlocksWhenFull() {
    BoundedBlockingQueue<int> queue(1);

    queue.push(1);

    // async：异步执行另一个函数，类似启动一个新的生产者线程
    // 因为队列满时会阻塞，主线程永远无法运行
    // std::launch::async表示要求立即在独立执行线程中运行任务
    auto producer = std::async(
        std::launch::async,
        [&queue]() {
            return queue.push(2);
        }
    );

    // 等待异步任务最多50ms，但不会取出返回值
    const auto initial_status = producer.wait_for(50ms);

    expectTrue(
        initial_status == std::future_status::timeout,
        "producer waits while queue is full"
    );

    const auto first = queue.pop();

    expectTrue(
        first.has_value() &&
        *first == 1,
        "consumer removes first value"
    );

    const auto resumed_status = producer.wait_for(1s);

    expectTrue(
        resumed_status ==
            std::future_status::ready,
        "producer resumes after pop"
    );
    expectTrue(
        producer.get(),
        "resumed producer succeeds"
    );

    const auto second = queue.pop();

    expectTrue(
        second.has_value() &&
        *second == 2,
        "second value entered queue"
    );
}

void testConsumerBlocksWhenEmpty() {
    BoundedBlockingQueue<int> queue(1);

    auto consumer = std::async(
        std::launch::async,
        [&queue]() {
            return queue.pop();
        }
    );

    const auto initial_status = consumer.wait_for(50ms);

    expectTrue(
        initial_status == std::future_status::timeout,
        "consumer waits while queue is empty"
    );

    queue.push(42);

    const auto resumed_status = consumer.wait_for(1s);

    expectTrue(
        resumed_status == std::future_status::ready,
        "consumer resumes after push"
    );

    const auto value = consumer.get();

    expectTrue(
        value.has_value() && *value == 42,
        "consumer receives pushed value"
    );
}

void testCloseUnblocksConsumer() {
    BoundedBlockingQueue<int> queue(1);
    
    auto consumer = std::async(
        std::launch::async,
        [&queue]() {
            return queue.pop();
        }
    );

    expectTrue(
        consumer.wait_for(59ms) == std::future_status::timeout,
        "consumer waits before close"
    );

    queue.close();

    expectTrue(
        consumer.wait_for(1s) == std::future_status::ready,
        "close wakes blocked consumer"
    );

    const auto value = consumer.get();
    
    expectTrue(
        !value.has_value(),
        "closed empty queue returns nullopt"
    );
}

void testCloseUnblocksProducer() {
    BoundedBlockingQueue<int> queue(1);

    queue.push(1);

    auto producer = std::async(
        std::launch::async,
        [&queue]() {
            return queue.push(2);
        }
    );

    expectTrue(
        producer.wait_for(50ms) ==
            std::future_status::timeout,
        "producer waits before close"
    );

    queue.close();

    expectTrue(
        producer.wait_for(1s) == std::future_status::ready,
        "close wakes blocked producer"
    );

    expectTrue(
        !producer.get(),
        "push fails after queue closes"
    );

    const auto remaining = queue.pop();

    expectTrue(
        remaining.has_value() &&
        *remaining == 1,
        "existing element remains after close"
    );
    // 由于已关闭，因此不会阻塞
    expectTrue(
        !queue.pop().has_value(),
        "closed drained queue returns nullopt"
    );
}

void testCloseDrainsExistingItems() {
    BoundedBlockingQueue<int> queue(2);

    queue.push(1);
    queue.push(2);

    queue.close();

    const auto first = queue.pop();
    const auto second = queue.pop();
    const auto finished = queue.pop();

    expectTrue(
        first.has_value() &&
        *first == 1,
        "close preserves first queued item"
    );
    expectTrue(
        second.has_value() &&
        *second == 2,
        "close preserves second queued item"
    );
    expectTrue(
        !finished.has_value(),
        "closed queue finishes after drain"
    );
}

void testClearRemovesPendingItems() {
    BoundedBlockingQueue<std::string> queue(3);

    queue.push("old sentence 1");
    queue.push("old sentence 2");

    const std::size_t removed = queue.clear();

    expectTrue(
        removed == 2,
        "clear reports removed count"
    );
    expectTrue(
        queue.empty(),
        "clear removes queued items"
    );
    expectTrue(
        queue.push("new sentence"),
        "queue accepts new items after clear"
    );

    const auto value = queue.pop();

    expectTrue(
        value.has_value() &&
        *value == "new sentence",
        "new item is available after clear"
    );
}

void testClearUnblocksProducer() {
    using namespace std::chrono_literals;
    BoundedBlockingQueue<int> queue(1);
    queue.push(1);
    std::promise<void> entered;
    auto producer = std::async(std::launch::async, [&] {
        entered.set_value();
        return queue.push(2);
    });
    entered.get_future().wait();
    expectTrue(producer.wait_for(50ms) == std::future_status::timeout,
               "producer waits before clear");
    expectTrue(queue.clear() == 1, "clear removes the full queue");
    expectTrue(producer.wait_for(1s) == std::future_status::ready,
               "clear wakes blocked producer");
    expectTrue(producer.get(), "producer succeeds after clear");
    const auto value = queue.pop();
    expectTrue(value.has_value() && *value == 2, "post-clear item is preserved");
}

void testPushMetrics() {
    using namespace std::chrono_literals;
    BoundedBlockingQueue<int> queue(1);
    QueuePushMetrics first;
    expectTrue(queue.push(1, first), "metric push succeeds");
    expectTrue(!first.waited && first.wait_elapsed_ms == 0.0 && first.depth_after_push == 1,
               "unblocked push has exact zero capacity wait and locked depth");
    QueuePushMetrics blocked;
    std::promise<void> entered;
    auto producer = std::async(std::launch::async, [&] {
        entered.set_value();
        return queue.push(2, blocked);
    });
    entered.get_future().wait();
    expectTrue(producer.wait_for(50ms) == std::future_status::timeout,
               "metric producer is held by full capacity");
    queue.pop();
    expectTrue(producer.wait_for(1s) == std::future_status::ready, "metric producer resumes");
    expectTrue(producer.get(), "metric producer succeeds");
    expectTrue(blocked.waited && blocked.wait_elapsed_ms > 0.0 && blocked.depth_after_push == 1,
               "capacity wait is observed");
    queue.close();
    QueuePushMetrics rejected;
    expectTrue(!queue.push(3, rejected) && !rejected.waited && rejected.depth_after_push == 0,
               "closed push has no invented depth or wait");
}

void testMoveOnlyValue() {
    BoundedBlockingQueue<std::unique_ptr<int>> queue(1);

    auto value = std::make_unique<int>(123);

    expectTrue(
        queue.push(std::move(value)),
        "push move-only value"
    );
    expectTrue(
        value == nullptr,
        "ownership moved into queue"
    );

    auto received = queue.pop();

    expectTrue(
        received.has_value(),
        "pop move-only value"
    );
    expectTrue(
        *received != nullptr && **received == 123,
        "move-only value is preserved"
    );
}

void testQueueState() {
    BoundedBlockingQueue<int> queue(4);

    expectTrue(
        queue.capacity() == 4,
        "report queue capacity"
    );
    expectTrue(
        queue.empty(),
        "new queue is empty"
    );
    expectTrue(
        !queue.closed(),
        "new queue is open"
    );

    queue.push(1);

    expectTrue(
        queue.size() == 1,
        "report queue size"
    );

    queue.close();

    expectTrue(
        queue.closed(),
        "report closed state"
    );
}

void testRejectZeroCapacity() {
    bool rejected = false;

    try {
        BoundedBlockingQueue<int> queue(0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    expectTrue(
        rejected,
        "reject zero queue capacity"
    );
}

}   // namespace

int main() {
    testFifoOrder();
    testProducerBlocksWhenFull();
    testConsumerBlocksWhenEmpty();
    testCloseUnblocksConsumer();
    testCloseUnblocksProducer();
    testCloseDrainsExistingItems();
    testClearRemovesPendingItems();
    testClearUnblocksProducer();
    testPushMetrics();
    testMoveOnlyValue();
    testQueueState();
    testRejectZeroCapacity();

    if (failed_count == 0) {
        std::cout
            << "\nAll bounded blocking queue "
            << "tests passed.\n";

        return 0;
    }

    std::cout
        << "\nFailed tests: "
        << failed_count
        << '\n';

    return 1;
}
