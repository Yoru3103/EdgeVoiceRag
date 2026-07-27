#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

template <typename T>
class BoundedBlockingQueue {
public:
    explicit BoundedBlockingQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("queue capacity must be greater than zero");
        }
    }

    BoundedBlockingQueue(const BoundedBlockingQueue&) = delete;
    BoundedBlockingQueue& operator=(const BoundedBlockingQueue&) = delete;

    bool push(const T& value) {
        return pushImpl(value);
    }

    // 支持移动， audiobuffer较大，避免复制PCM
    bool push(T&& value) {
        return pushImpl(std::move(value));
    }

    std::optional<T> pop() {
        // unique_lock支持手动上锁关锁（lock(), unlock()）
        std::unique_lock<std::mutex> lock(mutex_);

        // 消费者等待队列有数据
        // 条件不满足时自动休眠，并释放对应锁
        // lambda函数为条件谓词，条件变量可能发生虚假唤醒，相当于二次验证
        not_empty_.wait(
            lock,
            [this]() {
                return (closed_ || !queue_.empty());
            }
        );

        // 队列关闭且没有剩余元素时，表示消费者应该退出
        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());

        queue_.pop_front();

        lock.unlock();

        not_full_.notify_one();

        return value;
    }

    void close() {
        {
            // lock_guard只能通过构造和析构上锁，不支持lock和unlock
            std::lock_guard<std::mutex> lock(mutex_);

            closed_ = true;
        }

        // 必须同时唤醒生存者和消费者
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t clear() {
        std::size_t removed_count = 0;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            removed_count = queue_.size();
            queue_.clear();
        }

        // 清空队列后出现可用空间，应唤醒等待中的生产者
        not_full_.notify_all();

        return removed_count;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);

        return queue_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);

        return queue_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);

        return closed_;
    }

private:
    template <typename U>
    bool pushImpl(U&& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(
            lock,
            [this]() {
                return (closed_ || queue_.size() < capacity());
            }
        );

        if (closed_) {
            return false;
        }

        // forward保留传入参数的左值右值属性
        queue_.emplace_back(std::forward<U>(value));

        lock.unlock();

        not_empty_.notify_one();

        return true;
    }

    const std::size_t capacity_;

    // 成员函数empty()和size()因const不能修改普通成员，而mutex_上锁会修改变量
    // mutable允许const对象修改互斥锁
    mutable std::mutex mutex_;

    std::condition_variable not_empty_;
    std::condition_variable not_full_;

    std::deque<T> queue_;

    bool closed_ = false;
};
