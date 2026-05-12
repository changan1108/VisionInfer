#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <atomic>
#include <ctime>
#include <cstdint>
#include <string>

struct SystemStatusSnapshot
{
    int currentConnections = 0;
    int peakConnections = 0;
    int pendingTasks = 0;
    int peakPendingTasks = 0;
    int activeThreads = 0;
    int peakActiveThreads = 0;
    int completedTasks = 0;
    int failedTasks = 0;
    std::uint64_t totalRequests = 0;
    std::uint64_t totalTaskDurationMs = 0;
    std::uint64_t avgTaskDurationMs = 0;
    std::uint64_t maxTaskDurationMs = 0;
    std::uint64_t uptimeSeconds = 0;
    double requestThroughputPerSecond = 0.0;
    double taskSuccessRate = 0.0;
    std::string startedAt;
};

class SystemMonitor
{
public:
    static SystemMonitor &instance();

    void markServerStarted();

    void incrementConnections();
    void decrementConnections();

    void incrementPendingTasks();
    void decrementPendingTasks();

    void incrementActiveThreads();
    void decrementActiveThreads();

    void incrementCompletedTasks();
    void incrementFailedTasks();
    void incrementTotalRequests();
    void recordTaskDuration(std::uint64_t duration_ms);

    SystemStatusSnapshot snapshot() const;

private:
    SystemMonitor();

    std::atomic<int> current_connections_;
    std::atomic<int> peak_connections_;
    std::atomic<int> pending_tasks_;
    std::atomic<int> peak_pending_tasks_;
    std::atomic<int> active_threads_;
    std::atomic<int> peak_active_threads_;
    std::atomic<int> completed_tasks_;
    std::atomic<int> failed_tasks_;
    std::atomic<std::uint64_t> total_requests_;
    std::atomic<std::uint64_t> total_task_duration_ms_;
    std::atomic<std::uint64_t> max_task_duration_ms_;
    std::string started_at_;
    std::time_t started_at_epoch_;
};

#endif // SYSTEM_MONITOR_H
