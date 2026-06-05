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
    // 构造函数:在线程池对象构造时，创建出固定数量的线程放入线程池
    explicit ThreadPool(std::size_t thread_count, std::size_t max_queue_size = 0)
        : max_queue_size_(max_queue_size), stop_(false)
    {
        if (thread_count == 0)
        {
            throw std::invalid_argument("ThreadPool thread_count must be greater than zero");
        }

        // vector预先分配
        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            // 加入一个元素，配合外部的循环，从而创建出固定数量的线程
            workers_.emplace_back([this]()
                                  {
                // 每个线程执行这个for逻辑
                for (;;)
                {
                    // 准备一个“可调用对象包装器”对象，准备接收即将要执行的任务
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);// 加锁
                        // 如果stop_ 为false且队列为空，就睡眠等待
                        // 如果stop_为true或者队列不空，直接跳过wait，取任务执行即可
                        // 当任务队列为空，说明当前线程无新任务，才会将线程阻塞在条件变量上，因为不阻塞后续也无执行内容；
                        // 当任务队列不空，说明当前线程可以拿新任务，不会阻塞他，让他直接去取任务
                        condition_.wait(lock, [this]() {
                            return stop_ || !tasks_.empty();
                        });

                        // 被唤醒后，检查线程池是否停止，如果停止，则说明shutdown生效，当前线程return退出即可
                        if (stop_ && tasks_.empty())
                        {
                            return;
                        }

                        // 从队列中取一个任务(使用“移动赋值”)
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }// 出作用域自动解锁

                    // 执行任务(函数签名+"()"+传入参数，由于是无参的，所以+"()"即可)
                    task();
                }// 回到循环继续等下一个任务
            });
        }
    }

    ~ThreadPool()
    {
        // 线程池对象销毁时，会自动关闭线程池
        shutdown();
    }

    // 入队函数，他可以把任意函数(包括有参+返回值非void)包装成无参+返回值void的lambda函数对象
    template <class F, class... Args>
    auto enqueue(F &&func, Args &&...args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        //  定义ReturnType为"用 Args...调用F的返回类型"(起别名)
        using ReturnType = typename std::result_of<F(Args...)>::type;

        // 核心包装：把传进来的函数 func 包装成一个可执行任务，并且配套生成 future，方便理论上拿到执行结果
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            // 把函数和参数提前绑定成一个无参函数，即不管原函数有无参数，都会被包装成"无参lambda"(在lambda函数体里再执行有参函数,通过捕捉带进去参数)
            std::bind(std::forward<F>(func), std::forward<Args>(args)...));
        
        // 获取future，最后会返回他
        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);// 加锁
            
            // 检查出线程池已经停止
            if (stop_)
            {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            // 检查出队列满了
            if (max_queue_size_ > 0 && tasks_.size() >= max_queue_size_)
            {
                throw ThreadPoolQueueFull("thread pool queue is full");
            }

            // 真正放入队列
            // 用lambda再次包装，使其返回值为void(本lambda没有定义返回值，所以，是void)，而是真正的返回值在future里
            tasks_.emplace([task]()
                           { (*task)(); });
        }

        // 通知一个正在 condition_.wait(...) 上等待的线程
        // 有线程阻塞，那正好唤醒他让其从队列拿取新任务
        // 无线程阻塞，那说明线程都在忙碌，通知会无效，
        // 但是因为已经入队了，所以后续有线程解放后，会看到队列不空，不会阻塞条件变量，直接去队列中拿取新任务
        condition_.notify_one();

        // 返回其因为包装被隐藏的返回值
        return result;
    }

    // 关闭线程池
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);// 加锁
            if (stop_)
            {
                return;
            }

            // 设置停止标志
            stop_ = true;
        }// 走出作用域自动解锁

        // 唤醒所有等待线程，防止析构时join()会卡住
        condition_.notify_all();

        // 等待所有线程退出
        for (std::thread &worker : workers_)
        {
            if (worker.joinable())
            {
                // 主线程(当前执行for循环的线程)等待工作线程(workers_的成员)结束，避免线程对象析构时程序异常终止
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
    // std::function<void()>:通用可调用对象包装器,可以保存任何“没有参数、没有返回值”的可调用对象(比如函数、lambda函数对象)
    std::queue<std::function<void()>> tasks_;
    const std::size_t max_queue_size_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

#endif // THREAD_POOL_H
