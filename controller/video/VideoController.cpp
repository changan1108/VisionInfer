#include "controller/video/VideoController.h"

#include "entity/TaskEntity.h"
#include "service/task/TaskService.h"

#include <json/json.h>
#include <vector>

namespace
{
const std::vector<std::string> kSupportedTaskTypes = {
    "violation_detection",
    "vehicle_detection",
    "knife_detection"};

bool isSupportedTaskType(const std::string &task_type)
{
    for (std::vector<std::string>::const_iterator it = kSupportedTaskTypes.begin(); it != kSupportedTaskTypes.end(); ++it)
    {
        if (*it == task_type)
        {
            return true;
        }
    }
    return false;
}
}

void VideoController::initRoutes(Router *router)
{
    router->addRoute("POST", "/api/task/submit", VideoController::handleSubmitTask);
    router->addRoute("GET", "/api/task/status", VideoController::handleGetTaskStatus);
}

void VideoController::handleSubmitTask(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(req.body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
        return;
    }

    TaskEntity task;
    task.task_name = root["task_name"].asString();
    task.task_type = root["task_type"].asString();
    task.submitted_by = root["submitted_by"].asString();
    task.input_video_path = root["input_video_path"].asString();
    task.frame_interval = root.isMember("frame_interval") ? root["frame_interval"].asInt() : 1;
    task.confidence_threshold = root.isMember("confidence_threshold") ? root["confidence_threshold"].asDouble() : 0.5;
    task.model_id = root.isMember("model_id") ? root["model_id"].asInt() : 0;

    if (task.submitted_by.empty() || task.input_video_path.empty() || task.task_type.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "task_type、submitted_by 和 input_video_path 为必填项"})";
        return;
    }

    if (!isSupportedTaskType(task.task_type))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "task_type 不受支持，可选值：violation_detection、vehicle_detection、knife_detection"})";
        return;
    }

    if (task.frame_interval <= 0)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "frame_interval 必须大于 0"})";
        return;
    }

    if (task.confidence_threshold <= 0.0 || task.confidence_threshold > 1.0)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "confidence_threshold 必须在 (0, 1] 范围内"})";
        return;
    }

    if (!TaskService::submitTask(task))
    {
        res.statusCode = 500;
        res.body = R"({"code": 500, "msg": "任务提交失败"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "任务提交成功";
    response["data"]["task_id"] = Json::Int64(task.id);
    response["data"]["status"] = task.status;
    response["data"]["task_name"] = task.task_name;
    response["data"]["task_type"] = task.task_type;

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void VideoController::handleGetTaskStatus(const HttpRequest &req, HttpResponse &res)
{
    auto it = req.queryParams.find("id");
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少任务 id 参数"})";
        return;
    }

    long long task_id = 0;
    try
    {
        task_id = std::stoll(it->second);
    }
    catch (...)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "任务 id 格式错误"})";
        return;
    }

    TaskEntity task;
    if (!TaskService::getTaskById(task_id, task))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "任务不存在"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询任务成功";
    response["data"]["id"] = Json::Int64(task.id);
    response["data"]["task_name"] = task.task_name;
    response["data"]["task_type"] = task.task_type;
    response["data"]["submitted_by"] = task.submitted_by;
    response["data"]["input_video_path"] = task.input_video_path;
    response["data"]["output_video_path"] = task.output_video_path;
    response["data"]["frame_interval"] = task.frame_interval;
    response["data"]["confidence_threshold"] = task.confidence_threshold;
    response["data"]["status"] = task.status;
    response["data"]["result_summary"] = task.result_summary;
    response["data"]["error_message"] = task.error_message;
    response["data"]["model_id"] = task.model_id;
    response["data"]["created_at"] = task.created_at;
    response["data"]["started_at"] = task.started_at;
    response["data"]["finished_at"] = task.finished_at;

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}
