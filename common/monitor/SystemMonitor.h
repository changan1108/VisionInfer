#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <atomic>
#include <ctime>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    int rejectedTasks = 0;
    std::uint64_t totalRequests = 0;
    std::uint64_t totalTaskDurationMs = 0;
    std::uint64_t totalQueueWaitMs = 0;
    std::uint64_t avgQueueWaitMs = 0;
    std::uint64_t maxQueueWaitMs = 0;
    std::uint64_t avgTaskDurationMs = 0;
    std::uint64_t maxTaskDurationMs = 0;
    std::uint64_t uptimeSeconds = 0;
    double requestThroughputPerSecond = 0.0;
    double taskSuccessRate = 0.0;
    double tasksPerMinute = 0.0;
    double currentFpsTotal = 0.0;
    std::uint64_t p95TaskDurationMs = 0;
    std::uint64_t acceptedConnections = 0;
    std::uint64_t disconnectedConnections = 0;
    int onlineClients = 0;
    int peakOnlineClients = 0;
    std::string latestError;
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
    void incrementRejectedTasks();
    void incrementTotalRequests();
    void recordClientHeartbeat(const std::string &client_id);
    void recordTaskQueueWait(std::uint64_t wait_ms);
    void recordTaskDuration(std::uint64_t duration_ms);
    void recordTaskFrames(int processed_frame_count, std::uint64_t duration_ms);

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
    std::atomic<int> rejected_tasks_;
    std::atomic<std::uint64_t> total_requests_;
    std::atomic<std::uint64_t> total_task_duration_ms_;
    std::atomic<std::uint64_t> total_queue_wait_ms_;
    std::atomic<std::uint64_t> max_queue_wait_ms_;
    std::atomic<std::uint64_t> max_task_duration_ms_;
    std::atomic<std::uint64_t> accepted_connections_;
    std::atomic<std::uint64_t> disconnected_connections_;
    std::atomic<int> peak_online_clients_;
    std::string started_at_;
    std::time_t started_at_epoch_;
    mutable std::mutex online_clients_mutex_;
    std::unordered_map<std::string, std::time_t> online_clients_;
    mutable std::mutex durations_mutex_;
    std::vector<std::uint64_t> recent_task_durations_ms_;
    mutable std::mutex fps_samples_mutex_;
    struct FpsSample
    {
        std::time_t recorded_at = 0;
        int processed_frames = 0;
        std::uint64_t duration_ms = 0;
    };
    std::vector<FpsSample> recent_fps_samples_;
    mutable std::mutex latest_error_mutex_;
    std::string latest_error_;
};

#endif // SYSTEM_MONITOR_H
