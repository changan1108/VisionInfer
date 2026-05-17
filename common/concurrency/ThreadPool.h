#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

class ThreadPoolQueueFull : public std::runtime_error
{
public:
    explicit ThreadPoolQueueFull(const std::string &message)
        : std::runtime_error(message)
    {
    }
};

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t thread_count, std::size_t max_queue_size = 0)
        : max_queue_size_(max_queue_size), stop_(false)
    {
        if (thread_count == 0)
        {
            throw std::invalid_argument("ThreadPool thread_count must be greater than zero");
        }

        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            workers_.emplace_back([this]()
                                  {
                for (;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this]() {
                            return stop_ || !tasks_.empty();
                        });

                        if (stop_ && tasks_.empty())
                        {
                            return;
                        }

                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }

                    task();
                } });
        }
    }

    ~ThreadPool()
    {
        shutdown();
    }

    template <class F, class... Args>
    auto enqueue(F &&func, Args &&...args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using ReturnType = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(func), std::forward<Args>(args)...));

        std::future<ReturnType> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
            {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            if (max_queue_size_ > 0 && tasks_.size() >= max_queue_size_)
            {
                throw ThreadPoolQueueFull("thread pool queue is full");
            }

            tasks_.emplace([task]()
                           { (*task)(); });
        }

        condition_.notify_one();
        return result;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
            {
                return;
            }
            stop_ = true;
        }

        condition_.notify_all();
        for (std::thread &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    std::size_t size() const
    {
        return workers_.size();
    }

    std::size_t queueSize() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    std::size_t queueCapacity() const
    {
        return max_queue_size_;
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    const std::size_t max_queue_size_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

#endif // THREAD_POOL_H
