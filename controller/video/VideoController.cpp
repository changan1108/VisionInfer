#include "controller/video/VideoController.h"

#include "entity/TaskEntity.h"
#include "service/task/TaskService.h"
#include "service/video/VideoLibraryService.h"
#include "service/video/VideoUploadService.h"

#include <fstream>
#include <json/json.h>
#include <algorithm>
#include <cctype>
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

std::string toLowerCopy(const std::string &input)
{
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string getHeaderIgnoreCase(const std::unordered_map<std::string, std::string> &headers,
                                const std::string &target_key)
{
    std::string lowered_target = toLowerCopy(target_key);
    for (std::unordered_map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it)
    {
        if (toLowerCopy(it->first) == lowered_target)
        {
            return it->second;
        }
    }
    return "";
}

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

Json::Value buildTaskJson(const TaskEntity &task)
{
    Json::Value item;
    item["id"] = Json::Int64(task.id);
    item["task_name"] = task.task_name;
    item["task_type"] = task.task_type;
    item["submitted_by"] = task.submitted_by;
    item["input_video_path"] = task.input_video_path;
    item["input_video_id"] = task.input_video_id;
    item["output_video_path"] = task.output_video_path;
    item["video_duration"] = task.video_duration;
    item["video_width"] = task.video_width;
    item["video_height"] = task.video_height;
    item["video_fps"] = task.video_fps;
    item["result_url"] = task.output_video_path.empty() ? "" : "/api/video/result?task_id=" + std::to_string(task.id);
    item["frame_interval"] = task.frame_interval;
    item["confidence_threshold"] = task.confidence_threshold;
    item["processed_frame_count"] = task.processed_frame_count;
    item["detection_count"] = task.detection_count;
    item["real_inference_executed"] = task.real_inference_executed;
    item["result_video_generated"] = task.result_video_generated;
    item["used_model_name"] = task.used_model_name;
    item["used_model_framework"] = task.used_model_framework;
    item["video_build_mode"] = task.video_build_mode;
    item["inference_runtime_message"] = task.inference_runtime_message;
    item["status"] = task.status;
    item["result_summary"] = task.result_summary;
    item["error_message"] = task.error_message;
    item["model_id"] = task.model_id;
    item["created_at"] = task.created_at;
    item["started_at"] = task.started_at;
    item["finished_at"] = task.finished_at;

    // 这些字段方便前端直接做状态展示，不需要再解析 result_summary 文本。
    item["has_result_video"] = !task.output_video_path.empty();
    item["detection_summary_ready"] = !task.result_summary.empty();
    return item;
}

Json::Value buildVideoJson(const VideoEntity &video)
{
    Json::Value item;
    item["id"] = video.id;
    item["submitted_by"] = video.submitted_by;
    item["original_filename"] = video.original_filename;
    item["stored_filename"] = video.stored_filename;
    item["stored_path"] = video.stored_path;
    item["file_size_bytes"] = Json::Int64(video.file_size_bytes);
    item["duration"] = video.duration;
    item["width"] = video.width;
    item["height"] = video.height;
    item["fps"] = video.fps;
    item["uploaded_at"] = video.uploaded_at;
    item["preview_url"] = "/api/video/preview?id=" + std::to_string(video.id);
    return item;
}

bool parsePositiveInt(const std::string &text, int &out_value)
{
    try
    {
        out_value = std::stoi(text);
    }
    catch (...)
    {
        return false;
    }
    return out_value > 0;
}
}

void VideoController::initRoutes(Router *router)
{
    router->addRoute("POST", "/api/video/upload", VideoController::handleUploadVideo);
    router->addRoute("GET", "/api/video/list", VideoController::handleListVideos);
    router->addRoute("GET", "/api/video/info", VideoController::handleGetVideoInfo);
    router->addRoute("GET", "/api/video/preview", VideoController::handlePreviewVideo);
    router->addRoute("POST", "/api/task/submit", VideoController::handleSubmitTask);
    router->addRoute("GET", "/api/task/status", VideoController::handleGetTaskStatus);
    router->addRoute("GET", "/api/task/list", VideoController::handleListTasks);
    router->addRoute("GET", "/api/task/stats", VideoController::handleGetTaskStats);
    router->addRoute("GET", "/api/video/result", VideoController::handleGetResultVideo);
}

void VideoController::handleUploadVideo(const HttpRequest &req, HttpResponse &res)
{
    std::string content_type = getHeaderIgnoreCase(req.headers, "Content-Type");

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
        if (!parseJsonObjectBody(req.body, root, res))
        {
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

    if (!VideoLibraryService::registerUploadedVideo(request, result, error_message))
    {
        res.statusCode = 500;
        res.body = std::string("{\"code\": 500, \"msg\": \"") + error_message + "\"}";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "视频上传成功";
    response["data"]["video_id"] = result.video_id;
    response["data"]["submitted_by"] = request.submitted_by;
    response["data"]["original_filename"] = result.original_filename;
    response["data"]["stored_filename"] = result.stored_filename;
    response["data"]["stored_path"] = result.stored_path;
    response["data"]["file_size_bytes"] = Json::Int64(result.file_size_bytes);
    response["data"]["duration"] = result.duration;
    response["data"]["width"] = result.width;
    response["data"]["height"] = result.height;
    response["data"]["fps"] = result.fps;
    response["data"]["preview_url"] = "/api/video/preview?id=" + std::to_string(result.video_id);
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
    if (!parseJsonObjectBody(req.body, root, res))
    {
        return;
    }

    TaskEntity task;
    task.task_name = root["task_name"].asString();
    task.task_type = root["task_type"].asString();
    task.submitted_by = root["submitted_by"].asString();
    task.input_video_path = root["input_video_path"].asString();
    task.input_video_id = root.isMember("input_video_id") ? root["input_video_id"].asInt() : 0;
    task.frame_interval = root.isMember("frame_interval") ? root["frame_interval"].asInt() : 1;
    task.confidence_threshold = root.isMember("confidence_threshold") ? root["confidence_threshold"].asDouble() : 0.5;
    task.model_id = root.isMember("model_id") ? root["model_id"].asInt() : 0;

    if (task.input_video_id > 0 && task.input_video_path.empty())
    {
        std::string resolved_path;
        if (!VideoLibraryService::resolveVideoPathById(task.input_video_id, resolved_path))
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "input_video_id 不存在"})";
            return;
        }
        task.input_video_path = resolved_path;
    }

    if (task.submitted_by.empty() || task.input_video_path.empty() || task.task_type.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "task_type、submitted_by 和 input_video_path/input_video_id 为必填项"})";
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
    response["data"]["input_video_id"] = task.input_video_id;
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
    response["data"] = buildTaskJson(task);

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void VideoController::handleListTasks(const HttpRequest &req, HttpResponse &res)
{
    TaskListFilter filter;
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("limit");
    if (it != req.queryParams.end() && !it->second.empty())
    {
        try
        {
            filter.limit = std::stoi(it->second);
        }
        catch (...)
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "limit 参数格式错误"})";
            return;
        }
    }

    if (filter.limit <= 0 || filter.limit > 100)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "limit 取值范围必须在 1 到 100 之间"})";
        return;
    }

    it = req.queryParams.find("status");
    if (it != req.queryParams.end())
    {
        filter.status = it->second;
    }

    it = req.queryParams.find("task_type");
    if (it != req.queryParams.end())
    {
        filter.task_type = it->second;
    }

    it = req.queryParams.find("submitted_by");
    if (it != req.queryParams.end())
    {
        filter.submitted_by = it->second;
    }

    std::vector<TaskEntity> tasks = TaskService::listTasks(filter);

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询任务列表成功";
    response["data"]["total"] = static_cast<Json::UInt64>(tasks.size());
    response["data"]["limit"] = filter.limit;
    response["data"]["filters"]["status"] = filter.status;
    response["data"]["filters"]["task_type"] = filter.task_type;
    response["data"]["filters"]["submitted_by"] = filter.submitted_by;

    for (std::size_t i = 0; i < tasks.size(); ++i)
    {
        response["data"]["items"].append(buildTaskJson(tasks[i]));
    }

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void VideoController::handleGetTaskStats(const HttpRequest &req, HttpResponse &res)
{
    (void)req;

    TaskStats stats = TaskService::getTaskStats();

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询任务统计成功";
    response["data"]["total"] = stats.total;
    response["data"]["result_video_generated"] = stats.result_video_generated;
    response["data"]["real_inference_executed"] = stats.real_inference_executed;

    for (std::map<std::string, int>::const_iterator it = stats.by_status.begin(); it != stats.by_status.end(); ++it)
    {
        response["data"]["by_status"][it->first] = it->second;
    }

    for (std::map<std::string, int>::const_iterator it = stats.by_task_type.begin(); it != stats.by_task_type.end(); ++it)
    {
        response["data"]["by_task_type"][it->first] = it->second;
    }

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void VideoController::handleListVideos(const HttpRequest &req, HttpResponse &res)
{
    VideoListFilter filter;
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("limit");
    if (it != req.queryParams.end() && !it->second.empty())
    {
        if (!parsePositiveInt(it->second, filter.limit) || filter.limit > 100)
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "limit 取值范围必须在 1 到 100 之间"})";
            return;
        }
    }

    it = req.queryParams.find("submitted_by");
    if (it != req.queryParams.end())
    {
        filter.submitted_by = it->second;
    }

    it = req.queryParams.find("keyword");
    if (it != req.queryParams.end())
    {
        filter.keyword = it->second;
    }

    std::vector<VideoEntity> videos = VideoLibraryService::listVideos(filter);

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询视频列表成功";
    response["data"]["total"] = static_cast<Json::UInt64>(videos.size());
    response["data"]["limit"] = filter.limit;
    response["data"]["filters"]["submitted_by"] = filter.submitted_by;
    response["data"]["filters"]["keyword"] = filter.keyword;
    for (std::size_t i = 0; i < videos.size(); ++i)
    {
        response["data"]["items"].append(buildVideoJson(videos[i]));
    }

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void VideoController::handleGetVideoInfo(const HttpRequest &req, HttpResponse &res)
{
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("id");
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少视频 id 参数"})";
        return;
    }

    int video_id = 0;
    if (!parsePositiveInt(it->second, video_id))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "视频 id 格式错误"})";
        return;
    }

    VideoInfoView info;
    if (!VideoLibraryService::getVideoInfo(video_id, info))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "视频不存在"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询视频详情成功";
    response["data"] = buildVideoJson(info.video);
    response["data"]["has_task_usage"] = info.has_task_usage;
    response["data"]["task_usage_count"] = info.task_usage_count;

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void VideoController::handlePreviewVideo(const HttpRequest &req, HttpResponse &res)
{
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("id");
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少视频 id 参数"})";
        return;
    }

    int video_id = 0;
    if (!parsePositiveInt(it->second, video_id))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "视频 id 格式错误"})";
        return;
    }

    std::string video_path;
    if (!VideoLibraryService::resolveVideoPathById(video_id, video_path))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "视频不存在"})";
        return;
    }

    std::ifstream input(video_path.c_str(), std::ios::binary);
    if (!input.is_open())
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "无法打开原始视频文件"})";
        return;
    }

    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    res.statusCode = 200;
    res.body = content;
    res.contentType = "video/mp4";
    res.contentDisposition = "inline";
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
