#include "controller/system/SystemController.h"

#include "common/monitor/SystemMonitor.h"
#include "service/model/ModelService.h"
#include "service/task/TaskService.h"

#include <json/json.h>

namespace
{
Json::Value buildModelJson(const ModelEntity &model)
{
    Json::Value item;
    item["id"] = model.id;
    item["model_name"] = model.model_name;
    item["file_path"] = model.file_path;
    item["framework"] = model.framework;
    item["is_active"] = model.is_active;
    item["uploaded_by"] = model.uploaded_by;
    item["uploaded_at"] = model.uploaded_at;
    item["updated_at"] = model.updated_at;
    return item;
}

Json::Value buildOverviewData()
{
    SystemStatusSnapshot snapshot = SystemMonitor::instance().snapshot();
    TaskStats task_stats = TaskService::getTaskStats();
    ModelStats model_stats = ModelService::getModelStats();

    Json::Value data;

    Json::Value runtime;
    runtime["current_connections"] = snapshot.currentConnections;
    runtime["peak_connections"] = snapshot.peakConnections;
    runtime["pending_tasks"] = snapshot.pendingTasks;
    runtime["peak_pending_tasks"] = snapshot.peakPendingTasks;
    runtime["active_threads"] = snapshot.activeThreads;
    runtime["peak_active_threads"] = snapshot.peakActiveThreads;
    runtime["completed_tasks"] = snapshot.completedTasks;
    runtime["failed_tasks"] = snapshot.failedTasks;
    runtime["total_requests"] = Json::UInt64(snapshot.totalRequests);
    runtime["uptime_seconds"] = Json::UInt64(snapshot.uptimeSeconds);
    runtime["request_throughput_per_second"] = snapshot.requestThroughputPerSecond;
    runtime["avg_task_duration_ms"] = Json::UInt64(snapshot.avgTaskDurationMs);
    runtime["max_task_duration_ms"] = Json::UInt64(snapshot.maxTaskDurationMs);
    runtime["task_success_rate"] = snapshot.taskSuccessRate;
    runtime["started_at"] = snapshot.startedAt;
    data["runtime"] = runtime;

    // 保留一份扁平字段，兼容之前已经对接过 /api/system/status 的前端调用方。
    data["current_connections"] = snapshot.currentConnections;
    data["peak_connections"] = snapshot.peakConnections;
    data["pending_tasks"] = snapshot.pendingTasks;
    data["peak_pending_tasks"] = snapshot.peakPendingTasks;
    data["active_threads"] = snapshot.activeThreads;
    data["peak_active_threads"] = snapshot.peakActiveThreads;
    data["completed_tasks"] = snapshot.completedTasks;
    data["failed_tasks"] = snapshot.failedTasks;
    data["total_requests"] = Json::UInt64(snapshot.totalRequests);
    data["uptime_seconds"] = Json::UInt64(snapshot.uptimeSeconds);
    data["request_throughput_per_second"] = snapshot.requestThroughputPerSecond;
    data["avg_task_duration_ms"] = Json::UInt64(snapshot.avgTaskDurationMs);
    data["max_task_duration_ms"] = Json::UInt64(snapshot.maxTaskDurationMs);
    data["task_success_rate"] = snapshot.taskSuccessRate;
    data["started_at"] = snapshot.startedAt;

    Json::Value tasks;
    tasks["total"] = task_stats.total;
    tasks["result_video_generated"] = task_stats.result_video_generated;
    tasks["real_inference_executed"] = task_stats.real_inference_executed;
    for (std::map<std::string, int>::const_iterator it = task_stats.by_status.begin(); it != task_stats.by_status.end(); ++it)
    {
        tasks["by_status"][it->first] = it->second;
    }
    for (std::map<std::string, int>::const_iterator it = task_stats.by_task_type.begin(); it != task_stats.by_task_type.end(); ++it)
    {
        tasks["by_task_type"][it->first] = it->second;
    }
    data["tasks"] = tasks;

    Json::Value models;
    models["total"] = model_stats.total;
    models["active_count"] = model_stats.active_count;
    models["has_active_model"] = model_stats.has_active_model;
    for (std::map<std::string, int>::const_iterator it = model_stats.by_framework.begin(); it != model_stats.by_framework.end(); ++it)
    {
        models["by_framework"][it->first] = it->second;
    }
    if (model_stats.has_active_model)
    {
        models["current_active_model"] = buildModelJson(model_stats.current_active_model);
    }
    data["models"] = models;

    return data;
}
}

void SystemController::initRoutes(Router *router)
{
    router->addRoute("GET", "/api/system/status", SystemController::handleGetSystemStatus);
    router->addRoute("GET", "/api/system/overview", SystemController::handleGetSystemOverview);
}

void SystemController::handleGetSystemStatus(const HttpRequest &, HttpResponse &res)
{
    Json::Value response;
    response["code"] = 200;
    response["msg"] = "获取系统状态成功";
    response["data"] = buildOverviewData();

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void SystemController::handleGetSystemOverview(const HttpRequest &, HttpResponse &res)
{
    Json::Value response;
    response["code"] = 200;
    response["msg"] = "获取系统概览成功";
    response["data"] = buildOverviewData();

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}
