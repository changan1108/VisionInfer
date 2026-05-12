#include "common/monitor/SystemMonitor.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

SystemMonitor::SystemMonitor()
    : current_connections_(0),
      peak_connections_(0),
      pending_tasks_(0),
      peak_pending_tasks_(0),
      active_threads_(0),
      peak_active_threads_(0),
      completed_tasks_(0),
      failed_tasks_(0),
      total_requests_(0),
      total_task_duration_ms_(0),
      max_task_duration_ms_(0),
      started_at_epoch_(0)
{
}

SystemMonitor &SystemMonitor::instance()
{
    static SystemMonitor instance;
    return instance;
}

void SystemMonitor::markServerStarted()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    started_at_epoch_ = now_c;
    std::tm tm_now = *std::localtime(&now_c);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    started_at_ = oss.str();
}

void SystemMonitor::incrementConnections()
{
    int current = current_connections_.fetch_add(1, std::memory_order_relaxed) + 1;
    int peak = peak_connections_.load(std::memory_order_relaxed);
    while (current > peak &&
           !peak_connections_.compare_exchange_weak(peak, current, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::decrementConnections()
{
    int current = current_connections_.load(std::memory_order_relaxed);
    while (current > 0 &&
           !current_connections_.compare_exchange_weak(current, current - 1, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::incrementPendingTasks()
{
    int current = pending_tasks_.fetch_add(1, std::memory_order_relaxed) + 1;
    int peak = peak_pending_tasks_.load(std::memory_order_relaxed);
    while (current > peak &&
           !peak_pending_tasks_.compare_exchange_weak(peak, current, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::decrementPendingTasks()
{
    int current = pending_tasks_.load(std::memory_order_relaxed);
    while (current > 0 &&
           !pending_tasks_.compare_exchange_weak(current, current - 1, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::incrementActiveThreads()
{
    int current = active_threads_.fetch_add(1, std::memory_order_relaxed) + 1;
    int peak = peak_active_threads_.load(std::memory_order_relaxed);
    while (current > peak &&
           !peak_active_threads_.compare_exchange_weak(peak, current, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::decrementActiveThreads()
{
    int current = active_threads_.load(std::memory_order_relaxed);
    while (current > 0 &&
           !active_threads_.compare_exchange_weak(current, current - 1, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::incrementCompletedTasks()
{
    completed_tasks_.fetch_add(1, std::memory_order_relaxed);
}

void SystemMonitor::incrementFailedTasks()
{
    failed_tasks_.fetch_add(1, std::memory_order_relaxed);
}

void SystemMonitor::incrementTotalRequests()
{
    total_requests_.fetch_add(1, std::memory_order_relaxed);
}

void SystemMonitor::recordTaskDuration(std::uint64_t duration_ms)
{
    total_task_duration_ms_.fetch_add(duration_ms, std::memory_order_relaxed);

    std::uint64_t current_max = max_task_duration_ms_.load(std::memory_order_relaxed);
    while (duration_ms > current_max &&
           !max_task_duration_ms_.compare_exchange_weak(current_max, duration_ms, std::memory_order_relaxed))
    {
    }
}

SystemStatusSnapshot SystemMonitor::snapshot() const
{
    SystemStatusSnapshot status;
    status.currentConnections = current_connections_.load(std::memory_order_relaxed);
    status.peakConnections = peak_connections_.load(std::memory_order_relaxed);
    status.pendingTasks = pending_tasks_.load(std::memory_order_relaxed);
    status.peakPendingTasks = peak_pending_tasks_.load(std::memory_order_relaxed);
    status.activeThreads = active_threads_.load(std::memory_order_relaxed);
    status.peakActiveThreads = peak_active_threads_.load(std::memory_order_relaxed);
    status.completedTasks = completed_tasks_.load(std::memory_order_relaxed);
    status.failedTasks = failed_tasks_.load(std::memory_order_relaxed);
    status.totalRequests = total_requests_.load(std::memory_order_relaxed);
    status.totalTaskDurationMs = total_task_duration_ms_.load(std::memory_order_relaxed);
    status.maxTaskDurationMs = max_task_duration_ms_.load(std::memory_order_relaxed);
    status.startedAt = started_at_;

    int total_finished_tasks = status.completedTasks + status.failedTasks;
    if (total_finished_tasks > 0)
    {
        status.avgTaskDurationMs = status.totalTaskDurationMs / static_cast<std::uint64_t>(total_finished_tasks);
        status.taskSuccessRate = static_cast<double>(status.completedTasks) / static_cast<double>(total_finished_tasks);
    }

    if (started_at_epoch_ > 0)
    {
        std::time_t now_c = std::time(nullptr);
        if (now_c >= started_at_epoch_)
        {
            status.uptimeSeconds = static_cast<std::uint64_t>(now_c - started_at_epoch_);
            if (status.uptimeSeconds > 0)
            {
                status.requestThroughputPerSecond = static_cast<double>(status.totalRequests) /
                                                   static_cast<double>(status.uptimeSeconds);
            }
        }
    }

    return status;
}
