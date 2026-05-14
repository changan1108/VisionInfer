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
      total_queue_wait_ms_(0),
      max_queue_wait_ms_(0),
      max_task_duration_ms_(0),
      accepted_connections_(0),
      disconnected_connections_(0),
      peak_online_clients_(0),
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
    accepted_connections_.fetch_add(1, std::memory_order_relaxed);
    int current = current_connections_.fetch_add(1, std::memory_order_relaxed) + 1;
    int peak = peak_connections_.load(std::memory_order_relaxed);
    while (current > peak &&
           !peak_connections_.compare_exchange_weak(peak, current, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::decrementConnections()
{
    disconnected_connections_.fetch_add(1, std::memory_order_relaxed);
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

void SystemMonitor::recordClientHeartbeat(const std::string &client_id)
{
    if (client_id.empty())
    {
        return;
    }

    std::time_t now_c = std::time(nullptr);
    int online_count = 0;
    {
        std::lock_guard<std::mutex> lock(online_clients_mutex_);
        online_clients_[client_id] = now_c;

        for (std::unordered_map<std::string, std::time_t>::iterator it = online_clients_.begin();
             it != online_clients_.end();)
        {
            if (now_c - it->second > 60)
            {
                it = online_clients_.erase(it);
            }
            else
            {
                ++it;
            }
        }

        online_count = static_cast<int>(online_clients_.size());
    }

    int peak = peak_online_clients_.load(std::memory_order_relaxed);
    while (online_count > peak &&
           !peak_online_clients_.compare_exchange_weak(peak, online_count, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::recordTaskQueueWait(std::uint64_t wait_ms)
{
    total_queue_wait_ms_.fetch_add(wait_ms, std::memory_order_relaxed);

    std::uint64_t current_max = max_queue_wait_ms_.load(std::memory_order_relaxed);
    while (wait_ms > current_max &&
           !max_queue_wait_ms_.compare_exchange_weak(current_max, wait_ms, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::recordTaskDuration(std::uint64_t duration_ms)
{
    total_task_duration_ms_.fetch_add(duration_ms, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(durations_mutex_);
        recent_task_durations_ms_.push_back(duration_ms);
        if (recent_task_durations_ms_.size() > 256)
        {
            recent_task_durations_ms_.erase(recent_task_durations_ms_.begin());
        }
    }

    std::uint64_t current_max = max_task_duration_ms_.load(std::memory_order_relaxed);
    while (duration_ms > current_max &&
           !max_task_duration_ms_.compare_exchange_weak(current_max, duration_ms, std::memory_order_relaxed))
    {
    }
}

void SystemMonitor::recordTaskFrames(int processed_frame_count, std::uint64_t duration_ms)
{
    if (processed_frame_count <= 0 || duration_ms == 0)
    {
        return;
    }

    std::time_t now_c = std::time(nullptr);
    std::lock_guard<std::mutex> lock(fps_samples_mutex_);
    FpsSample sample;
    sample.recorded_at = now_c;
    sample.processed_frames = processed_frame_count;
    sample.duration_ms = duration_ms;
    recent_fps_samples_.push_back(sample);

    while (!recent_fps_samples_.empty() && now_c - recent_fps_samples_.front().recorded_at > 60)
    {
        recent_fps_samples_.erase(recent_fps_samples_.begin());
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
    status.totalQueueWaitMs = total_queue_wait_ms_.load(std::memory_order_relaxed);
    status.maxQueueWaitMs = max_queue_wait_ms_.load(std::memory_order_relaxed);
    status.maxTaskDurationMs = max_task_duration_ms_.load(std::memory_order_relaxed);
    status.acceptedConnections = accepted_connections_.load(std::memory_order_relaxed);
    status.disconnectedConnections = disconnected_connections_.load(std::memory_order_relaxed);
    status.peakOnlineClients = peak_online_clients_.load(std::memory_order_relaxed);
    status.startedAt = started_at_;
    {
        std::lock_guard<std::mutex> lock(latest_error_mutex_);
        status.latestError = latest_error_;
    }
    {
        std::time_t now_c = std::time(nullptr);
        std::lock_guard<std::mutex> lock(online_clients_mutex_);
        for (std::unordered_map<std::string, std::time_t>::const_iterator it = online_clients_.begin();
             it != online_clients_.end(); ++it)
        {
            if (now_c - it->second <= 60)
            {
                ++status.onlineClients;
            }
        }
    }

    int total_finished_tasks = status.completedTasks + status.failedTasks;
    if (total_finished_tasks > 0)
    {
        status.avgTaskDurationMs = status.totalTaskDurationMs / static_cast<std::uint64_t>(total_finished_tasks);
        status.avgQueueWaitMs = status.totalQueueWaitMs / static_cast<std::uint64_t>(total_finished_tasks);
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
                status.tasksPerMinute = static_cast<double>(total_finished_tasks) * 60.0 /
                                        static_cast<double>(status.uptimeSeconds);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(durations_mutex_);
        if (!recent_task_durations_ms_.empty())
        {
            std::vector<std::uint64_t> sorted = recent_task_durations_ms_;
            std::sort(sorted.begin(), sorted.end());
            std::size_t idx = static_cast<std::size_t>(0.95 * static_cast<double>(sorted.size() - 1));
            status.p95TaskDurationMs = sorted[idx];
        }
    }
    {
        std::time_t now_c = std::time(nullptr);
        std::lock_guard<std::mutex> lock(fps_samples_mutex_);
        std::uint64_t total_duration_ms = 0;
        int total_frames = 0;
        for (std::vector<FpsSample>::const_iterator it = recent_fps_samples_.begin();
             it != recent_fps_samples_.end(); ++it)
        {
            if (now_c - it->recorded_at <= 60)
            {
                total_duration_ms += it->duration_ms;
                total_frames += it->processed_frames;
            }
        }
        if (total_duration_ms > 0)
        {
            status.currentFpsTotal = static_cast<double>(total_frames) * 1000.0 /
                                     static_cast<double>(total_duration_ms);
        }
    }

    return status;
}
