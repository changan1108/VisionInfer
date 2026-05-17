#include "controller/system/SystemController.h"

#include "common/config/AppConfig.h"
#include "common/monitor/SystemMonitor.h"
#include "service/task/TaskService.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <json/json.h>
#include <sstream>
#include <sys/statvfs.h>

namespace
{
bool parseJsonObjectBody(const std::string &body, Json::Value &root, HttpResponse &res)
{
    Json::Reader reader;
    if (!reader.parse(body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
        return false;
    }

    if (!root.isObject())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 请求体必须是对象"})";
        return false;
    }

    return true;
}

std::string formatNow()
{
    std::time_t now_c = std::time(nullptr);
    std::tm tm_now = *std::localtime(&now_c);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_now);
    return buffer;
}

double clampRatio(double value)
{
    if (value < 0.0)
    {
        return 0.0;
    }
    if (value > 1.0)
    {
        return 1.0;
    }
    return value;
}

bool readCpuUsage(double &out_usage)
{
    static bool has_previous = false;
    static unsigned long long prev_total = 0;
    static unsigned long long prev_idle = 0;

    std::ifstream input("/proc/stat");
    if (!input.is_open())
    {
        return false;
    }

    std::string cpu_label;
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    input >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    if (cpu_label != "cpu")
    {
        return false;
    }

    unsigned long long idle_all = idle + iowait;
    unsigned long long non_idle = user + nice + system + irq + softirq + steal;
    unsigned long long total = idle_all + non_idle;

    if (!has_previous || total <= prev_total)
    {
        prev_total = total;
        prev_idle = idle_all;
        has_previous = true;
        out_usage = 0.0;
        return true;
    }

    unsigned long long total_delta = total - prev_total;
    unsigned long long idle_delta = idle_all - prev_idle;
    prev_total = total;
    prev_idle = idle_all;
    out_usage = total_delta == 0 ? 0.0
                                 : 1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta);
    return true;
}

bool readMemoryUsage(double &out_usage)
{
    std::ifstream input("/proc/meminfo");
    if (!input.is_open())
    {
        return false;
    }

    std::string key;
    unsigned long long value = 0;
    std::string unit;
    unsigned long long total_kb = 0;
    unsigned long long available_kb = 0;
    while (input >> key >> value >> unit)
    {
        if (key == "MemTotal:")
        {
            total_kb = value;
        }
        else if (key == "MemAvailable:")
        {
            available_kb = value;
        }
    }

    if (total_kb == 0)
    {
        return false;
    }

    out_usage = 1.0 - static_cast<double>(available_kb) / static_cast<double>(total_kb);
    return true;
}

bool readDiskUsage(const std::string &path, double &out_usage)
{
    struct statvfs stat {};
    if (statvfs(path.c_str(), &stat) != 0 || stat.f_blocks == 0)
    {
        return false;
    }

    unsigned long long total = static_cast<unsigned long long>(stat.f_blocks) * stat.f_frsize;
    unsigned long long available = static_cast<unsigned long long>(stat.f_bavail) * stat.f_frsize;
    if (total == 0)
    {
        return false;
    }

    out_usage = 1.0 - static_cast<double>(available) / static_cast<double>(total);
    return true;
}

Json::Value buildSystemStatusData(const HttpRequest *req)
{
    SystemStatusSnapshot snapshot = SystemMonitor::instance().snapshot();
    TaskStats task_stats = TaskService::getTaskStats();
    TaskExecutionPoolSnapshot pool_snapshot = TaskService::getExecutionPoolSnapshot();
    int adjusted_current_connections = snapshot.currentConnections;
    if (req != nullptr && req->method == "GET" && req->path == "/api/system/status" && adjusted_current_connections > 0)
    {
        --adjusted_current_connections;
    }

    const int dispatch_live_threads = static_cast<int>(pool_snapshot.dispatchLiveThreads);
    const int processing_live_threads = static_cast<int>(pool_snapshot.processingLiveThreads);
    const int live_threads = dispatch_live_threads + processing_live_threads;
    const int busy_threads = std::min(snapshot.activeThreads, live_threads);
    const int idle_threads = std::max(0, live_threads - busy_threads);
    const double thread_utilization = live_threads > 0
                                          ? static_cast<double>(busy_threads) / static_cast<double>(live_threads)
                                          : 0.0;
    const int dispatch_queue_capacity = static_cast<int>(pool_snapshot.dispatchQueueCapacity);
    const int processing_queue_capacity = static_cast<int>(pool_snapshot.processingQueueCapacity);
    const int total_queue_capacity = dispatch_queue_capacity + processing_queue_capacity;
    const int total_waiting_tasks = static_cast<int>(pool_snapshot.dispatchQueueSize + pool_snapshot.processingQueueSize);
    const double queue_usage = total_queue_capacity > 0
                                   ? static_cast<double>(total_waiting_tasks) / static_cast<double>(total_queue_capacity)
                                   : 0.0;
    const int total_finished_tasks = snapshot.completedTasks + snapshot.failedTasks;
    const double result_video_generation_rate = total_finished_tasks > 0
                                                    ? static_cast<double>(task_stats.result_video_generated) / static_cast<double>(total_finished_tasks)
                                                    : 0.0;

    double cpu_usage = 0.0;
    double memory_usage = 0.0;
    double disk_usage = 0.0;
    bool has_cpu = readCpuUsage(cpu_usage);
    bool has_memory = readMemoryUsage(memory_usage);
    bool has_disk = readDiskUsage(AppConfig::PROJECT_ROOT, disk_usage);

    Json::Value data;
    data["snapshot_time"] = formatNow();

    Json::Value connection;
    connection["current_clients"] = snapshot.onlineClients;
    connection["peak_clients"] = snapshot.peakOnlineClients;
    connection["current_active_connections"] = adjusted_current_connections;
    connection["peak_active_connections"] = snapshot.peakConnections;
    data["connection"] = connection;

    Json::Value thread_pool;
    thread_pool["core_threads"] = static_cast<Json::UInt64>(live_threads);
    thread_pool["max_threads"] = static_cast<Json::UInt64>(live_threads);
    thread_pool["live_threads"] = live_threads;
    thread_pool["busy_threads"] = busy_threads;
    thread_pool["idle_threads"] = idle_threads;
    thread_pool["thread_utilization"] = clampRatio(thread_utilization);
    thread_pool["peak_live_threads"] = snapshot.peakActiveThreads;
    thread_pool["dispatch_live_threads"] = dispatch_live_threads;
    thread_pool["processing_live_threads"] = processing_live_threads;
    data["thread_pool"] = thread_pool;

    Json::Value task_queue;
    task_queue["waiting_tasks"] = total_waiting_tasks;
    task_queue["running_tasks"] = snapshot.activeThreads;
    task_queue["completed_tasks"] = snapshot.completedTasks;
    task_queue["failed_tasks"] = snapshot.failedTasks;
    task_queue["rejected_tasks"] = snapshot.rejectedTasks;
    task_queue["queue_capacity"] = total_queue_capacity;
    task_queue["queue_usage"] = clampRatio(queue_usage);
    task_queue["dispatch_waiting_tasks"] = static_cast<Json::UInt64>(pool_snapshot.dispatchQueueSize);
    task_queue["dispatch_queue_capacity"] = static_cast<Json::UInt64>(pool_snapshot.dispatchQueueCapacity);
    task_queue["processing_waiting_tasks"] = static_cast<Json::UInt64>(pool_snapshot.processingQueueSize);
    task_queue["processing_queue_capacity"] = static_cast<Json::UInt64>(pool_snapshot.processingQueueCapacity);
    task_queue["pending_tasks_total"] = snapshot.pendingTasks;
    task_queue["avg_queue_wait_ms"] = Json::UInt64(snapshot.avgQueueWaitMs);
    task_queue["max_queue_wait_ms"] = Json::UInt64(snapshot.maxQueueWaitMs);
    data["task_queue"] = task_queue;

    Json::Value inference;
    inference["current_fps_total"] = snapshot.currentFpsTotal;
    inference["avg_task_duration_ms"] = Json::UInt64(snapshot.avgTaskDurationMs);
    inference["p95_task_duration_ms"] = Json::UInt64(snapshot.p95TaskDurationMs);
    inference["max_task_duration_ms"] = Json::UInt64(snapshot.maxTaskDurationMs);
    inference["tasks_per_min"] = snapshot.tasksPerMinute;
    inference["success_rate"] = clampRatio(snapshot.taskSuccessRate);
    inference["result_video_generation_rate"] = clampRatio(result_video_generation_rate);
    data["inference"] = inference;

    Json::Value system;
    system["cpu_usage"] = has_cpu ? clampRatio(cpu_usage) : -1.0;
    system["memory_usage"] = has_memory ? clampRatio(memory_usage) : -1.0;
    system["disk_usage"] = has_disk ? clampRatio(disk_usage) : -1.0;
    system["uptime_seconds"] = Json::UInt64(snapshot.uptimeSeconds);
    data["system"] = system;

    return data;
}

}

void SystemController::initRoutes(Router *router)
{
    router->addRoute("POST", "/api/system/heartbeat", SystemController::handleHeartbeat);
    router->addRoute("GET", "/api/system/status", SystemController::handleGetSystemStatus);
}

void SystemController::handleHeartbeat(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    if (!parseJsonObjectBody(req.body, root, res))
    {
        return;
    }

    std::string username = root["username"].asString();
    if (username.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "username 为必填项"})";
        return;
    }

    SystemMonitor::instance().recordClientHeartbeat(username);

    res.statusCode = 200;
    res.body = R"({"code": 200, "msg": "heartbeat ok"})";
}

void SystemController::handleGetSystemStatus(const HttpRequest &req, HttpResponse &res)
{
    Json::Value response;
    response["code"] = 200;
    response["msg"] = "获取系统监控状态成功";
    response["data"] = buildSystemStatusData(&req);

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}
