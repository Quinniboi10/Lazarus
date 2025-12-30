#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};

public:
    explicit ThreadPool(const size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            threads_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(this->queueMutex_);

                        // Wait until stop is signaled AND queue is empty, or a new task arrives
                        this->condition_.wait(lock, [this] {
                            return this->stop_ || !this->queue_.empty();
                        });

                        // Exit if stop is true and the queue is exhausted
                        if (this->stop_ && this->queue_.empty())
                            return;

                        task = std::move(this->queue_.front());
                        this->queue_.pop();
                    }
                    task();
                }
            });
        }
    }

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard lock(queueMutex_);
            queue_.push(std::move(task));
        }
        condition_.notify_one();
    }

    ~ThreadPool() {
        {
            std::lock_guard lock(queueMutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread &worker : threads_) {
            if (worker.joinable())
                worker.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};