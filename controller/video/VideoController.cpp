#include "controller/video/VideoController.h"

#include "entity/TaskEntity.h"
#include "service/task/TaskService.h"
#include "service/video/VideoUploadService.h"

#include <fstream>
#include <json/json.h>
#include <map>
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

std::string extractBoundary(const std::string &content_type)
{
    std::size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos)
    {
        return "";
    }
    return content_type.substr(boundary_pos + 9);
}

bool parseMultipartForm(const std::string &body, const std::string &boundary,
                        std::map<std::string, std::string> &fields,
                        std::string &file_name, std::string &file_content)
{
    std::string delimiter = "--" + boundary;
    std::size_t cursor = 0;

    while (true)
    {
        std::size_t part_begin = body.find(delimiter, cursor);
        if (part_begin == std::string::npos)
        {
            break;
        }

        part_begin += delimiter.size();
        if (part_begin + 2 <= body.size() && body.substr(part_begin, 2) == "--")
        {
            break;
        }

        if (part_begin + 2 > body.size() || body.substr(part_begin, 2) != "\r\n")
        {
            cursor = part_begin;
            continue;
        }
        part_begin += 2;

        std::size_t headers_end = body.find("\r\n\r\n", part_begin);
        if (headers_end == std::string::npos)
        {
            return false;
        }

        std::string part_headers = body.substr(part_begin, headers_end - part_begin);
        std::size_t content_begin = headers_end + 4;
        std::size_t next_delimiter = body.find(delimiter, content_begin);
        if (next_delimiter == std::string::npos)
        {
            return false;
        }

        std::size_t content_end = next_delimiter;
        if (content_end >= 2 && body.substr(content_end - 2, 2) == "\r\n")
        {
            content_end -= 2;
        }
        std::string part_content = body.substr(content_begin, content_end - content_begin);

        std::size_t name_pos = part_headers.find("name=\"");
        if (name_pos == std::string::npos)
        {
            cursor = next_delimiter;
            continue;
        }

        name_pos += 6;
        std::size_t name_end = part_headers.find('"', name_pos);
        if (name_end == std::string::npos)
        {
            return false;
        }
        std::string part_name = part_headers.substr(name_pos, name_end - name_pos);

        std::size_t filename_pos = part_headers.find("filename=\"");
        if (filename_pos != std::string::npos)
        {
            filename_pos += 10;
            std::size_t filename_end = part_headers.find('"', filename_pos);
            if (filename_end == std::string::npos)
            {
                return false;
            }
            file_name = part_headers.substr(filename_pos, filename_end - filename_pos);
            file_content = part_content;
        }
        else
        {
            fields[part_name] = part_content;
        }

        cursor = next_delimiter;
    }

    return !file_content.empty();
}
}

void VideoController::initRoutes(Router *router)
{
    router->addRoute("POST", "/api/video/upload", VideoController::handleUploadVideo);
    router->addRoute("POST", "/api/task/submit", VideoController::handleSubmitTask);
    router->addRoute("GET", "/api/task/status", VideoController::handleGetTaskStatus);
    router->addRoute("GET", "/api/video/result", VideoController::handleGetResultVideo);
}

void VideoController::handleUploadVideo(const HttpRequest &req, HttpResponse &res)
{
    std::string content_type;
    std::unordered_map<std::string, std::string>::const_iterator header_it = req.headers.find("Content-Type");
    if (header_it != req.headers.end())
    {
        content_type = header_it->second;
    }

    VideoUploadRequest request;
    VideoUploadResult result;
    std::string error_message;

    if (content_type.find("multipart/form-data") != std::string::npos)
    {
        std::string boundary = extractBoundary(content_type);
        if (boundary.empty())
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "multipart/form-data 缺少 boundary"})";
            return;
        }

        std::map<std::string, std::string> fields;
        if (!parseMultipartForm(req.body, boundary, fields, request.original_filename, request.file_content))
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "multipart/form-data 解析失败"})";
            return;
        }

        request.submitted_by = fields["submitted_by"];
        if (!VideoUploadService::uploadFromBinary(request, result, error_message))
        {
            res.statusCode = 400;
            res.body = std::string("{\"code\": 400, \"msg\": \"") + error_message + "\"}";
            return;
        }
    }
    else
    {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(req.body, root))
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
            return;
        }

        request.submitted_by = root["submitted_by"].asString();
        request.source_file_path = root["source_file_path"].asString();
        request.original_filename = root.isMember("original_filename") ? root["original_filename"].asString() : "";

        if (!VideoUploadService::uploadFromServerPath(request, result, error_message))
        {
            res.statusCode = 400;
            res.body = std::string("{\"code\": 400, \"msg\": \"") + error_message + "\"}";
            return;
        }
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "视频上传成功";
    response["data"]["submitted_by"] = request.submitted_by;
    response["data"]["original_filename"] = result.original_filename;
    response["data"]["stored_filename"] = result.stored_filename;
    response["data"]["stored_path"] = result.stored_path;
    response["data"]["file_size_bytes"] = Json::Int64(result.file_size_bytes);
    response["data"]["upload_mode"] = content_type.find("multipart/form-data") != std::string::npos
                                          ? "multipart_form_data"
                                          : "placeholder_json_copy";

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
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

    std::string error_message;
    if (!TaskService::submitTask(task, error_message))
    {
        res.statusCode = 400;
        res.body = std::string("{\"code\": 400, \"msg\": \"") + error_message + "\"}";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "任务提交成功";
    response["data"]["task_id"] = Json::Int64(task.id);
    response["data"]["status"] = task.status;
    response["data"]["task_name"] = task.task_name;
    response["data"]["task_type"] = task.task_type;
    response["data"]["model_id"] = task.model_id;

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
    response["data"]["video_duration"] = task.video_duration;
    response["data"]["video_width"] = task.video_width;
    response["data"]["video_height"] = task.video_height;
    response["data"]["video_fps"] = task.video_fps;
    response["data"]["result_url"] = "/api/video/result?task_id=" + std::to_string(task.id);
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

void VideoController::handleGetResultVideo(const HttpRequest &req, HttpResponse &res)
{
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("task_id");
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少 task_id 参数"})";
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
        res.body = R"({"code": 400, "msg": "task_id 格式错误"})";
        return;
    }

    TaskEntity task;
    if (!TaskService::getTaskById(task_id, task))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "任务不存在"})";
        return;
    }

    if (task.output_video_path.empty())
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "任务结果文件不存在"})";
        return;
    }

    std::ifstream input(task.output_video_path.c_str(), std::ios::binary);
    if (!input.is_open())
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "无法打开结果视频文件"})";
        return;
    }

    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    res.statusCode = 200;
    res.body = content;
    res.contentType = "video/mp4";
    res.contentDisposition = "inline";
}
