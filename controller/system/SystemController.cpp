#include "controller/system/SystemController.h"

#include "common/monitor/SystemMonitor.h"

#include <json/json.h>

void SystemController::initRoutes(Router *router)
{
    router->addRoute("GET", "/api/system/status", SystemController::handleGetSystemStatus);
}

void SystemController::handleGetSystemStatus(const HttpRequest &, HttpResponse &res)
{
    SystemStatusSnapshot snapshot = SystemMonitor::instance().snapshot();

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "获取系统状态成功";

    Json::Value data;
    data["current_connections"] = snapshot.currentConnections;
    data["pending_tasks"] = snapshot.pendingTasks;
    data["active_threads"] = snapshot.activeThreads;
    data["completed_tasks"] = snapshot.completedTasks;
    data["failed_tasks"] = snapshot.failedTasks;
    data["total_requests"] = Json::UInt64(snapshot.totalRequests);
    data["started_at"] = snapshot.startedAt;

    response["data"] = data;

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}
