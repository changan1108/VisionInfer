#include "common/monitor/SystemMonitor.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

SystemMonitor::SystemMonitor()
    : current_connections_(0),
      pending_tasks_(0),
      active_threads_(0),
      completed_tasks_(0),
      failed_tasks_(0),
      total_requests_(0)
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
    std::tm tm_now = *std::localtime(&now_c);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    started_at_ = oss.str();
}

void SystemMonitor::incrementConnections()
{
    current_connections_.fetch_add(1, std::memory_order_relaxed);
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
    pending_tasks_.fetch_add(1, std::memory_order_relaxed);
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
    active_threads_.fetch_add(1, std::memory_order_relaxed);
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

SystemStatusSnapshot SystemMonitor::snapshot() const
{
    SystemStatusSnapshot status;
    status.currentConnections = current_connections_.load(std::memory_order_relaxed);
    status.pendingTasks = pending_tasks_.load(std::memory_order_relaxed);
    status.activeThreads = active_threads_.load(std::memory_order_relaxed);
    status.completedTasks = completed_tasks_.load(std::memory_order_relaxed);
    status.failedTasks = failed_tasks_.load(std::memory_order_relaxed);
    status.totalRequests = total_requests_.load(std::memory_order_relaxed);
    status.startedAt = started_at_;
    return status;
}
