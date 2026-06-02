#include "ModelController.h"

#include <algorithm>
#include <cctype>
#include <json/json.h>
#include <map>
#include <vector>

#include "entity/ModelEntity.h"
#include "service/auth/AuthService.h"
#include "service/model/ModelService.h"
#include "service/model/ModelUploadService.h"

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

bool parseBoolField(const std::string &input)
{
    std::string lowered = toLowerCopy(input);
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
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

int authErrorStatusCode(const std::string &error_message)
{
    if (error_message.find("禁用") != std::string::npos ||
        error_message.find("权限等级非法") != std::string::npos)
    {
        return 403;
    }
    return 401;
}

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
}

void ModelController::initRoutes(Router *router)
{
    router->addRoute("POST", "/api/model/upload", ModelController::handleUploadModel);
    router->addRoute("POST", "/api/model/add", ModelController::handleUploadModel);
    router->addRoute("POST", "/api/model/switch", ModelController::handleSwitchModel);
    router->addRoute("GET", "/api/model/current", ModelController::handleGetCurrentModel);
    router->addRoute("GET", "/api/model/list", ModelController::handleListModels);
    router->addRoute("DELETE", "/api/model", ModelController::handleDeleteModel);
}

void ModelController::handleUploadModel(const HttpRequest &req, HttpResponse &res)
{
    std::string content_type = getHeaderIgnoreCase(req.headers, "Content-Type");
    if (content_type.find("multipart/form-data") == std::string::npos)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "模型上传仅支持 multipart/form-data"})";
        return;
    }

    std::string boundary = extractBoundary(content_type);
    if (boundary.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "multipart/form-data 缺少 boundary"})";
        return;
    }

    std::map<std::string, std::string> fields;
    std::string file_name;
    std::string file_content;
    if (!parseMultipartForm(req.body, boundary, fields, file_name, file_content))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "multipart/form-data 解析失败"})";
        return;
    }

    ModelUploadRequest request;
    request.model_name = fields["model_name"];
    request.framework = fields["framework"];
    request.uploaded_by = fields["uploaded_by"];
    request.is_active = parseBoolField(fields["is_active"]);
    request.original_filename = file_name;
    request.file_content = file_content;

    ModelUploadResult result;
    std::string error_message;
    if (!ModelUploadService::uploadModel(request, result, error_message))
    {
        res.statusCode = 400;
        res.body = std::string("{\"code\": 400, \"msg\": \"") + error_message + "\"}";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "模型上传成功";
    response["data"] = buildModelJson(result.model);
    response["data"]["stored_filename"] = result.stored_filename;

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void ModelController::handleSwitchModel(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    if (!parseJsonObjectBody(req.body, root, res))
    {
        return;
    }

    int model_id = root.isMember("model_id") ? root["model_id"].asInt() : 0;
    if (model_id <= 0)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "model_id 必须大于 0"})";
        return;
    }

    std::string error_message;
    if (!ModelService::switchActiveModel(model_id, error_message))
    {
        res.statusCode = 400;
        res.body = std::string("{\"code\": 400, \"msg\": \"") + error_message + "\"}";
        return;
    }

    res.statusCode = 200;
    res.body = R"({"code": 200, "msg": "模型切换成功"})";
}

void ModelController::handleGetCurrentModel(const HttpRequest &, HttpResponse &res)
{
    ModelEntity model;
    if (!ModelService::getCurrentActiveModel(model))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "当前没有激活模型"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询当前模型成功";
    response["data"] = buildModelJson(model);

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void ModelController::handleListModels(const HttpRequest &, HttpResponse &res)
{
    std::vector<ModelEntity> models = ModelService::listModels();

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "查询模型列表成功";

    Json::Value data(Json::arrayValue);
    for (std::vector<ModelEntity>::const_iterator it = models.begin(); it != models.end(); ++it)
    {
        data.append(buildModelJson(*it));
    }
    response["data"] = data;

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void ModelController::handleDeleteModel(const HttpRequest &req, HttpResponse &res)
{
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("id");
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少模型 id 参数"})";
        return;
    }

    int model_id = 0;
    if (!parsePositiveInt(it->second, model_id))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "模型 id 格式错误"})";
        return;
    }

    OperatorContext operator_context;
    std::string auth_error;
    if (!AuthService::getOperatorContext(req, operator_context, auth_error))
    {
        res.statusCode = authErrorStatusCode(auth_error);
        res.body = std::string("{\"code\": ") + std::to_string(res.statusCode) + ", \"msg\": \"" + auth_error + "\"}";
        return;
    }

    ModelEntity model;
    if (!ModelService::getModelById(model_id, model))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "模型不存在或已删除"})";
        return;
    }

    if (!AuthService::canDeleteOwnedResource(operator_context, model.uploaded_by))
    {
        res.statusCode = 403;
        res.body = R"({"code": 403, "msg": "权限不足，只能删除自己上传的模型"})";
        return;
    }

    if (model.is_active)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "当前激活模型不允许删除，请先切换到其他模型"})";
        return;
    }

    if (!ModelService::softDeleteModelRecord(model_id, operator_context.username))
    {
        res.statusCode = 500;
        res.body = R"({"code": 500, "msg": "模型删除失败"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "模型删除成功";
    response["data"]["model_id"] = model_id;
    response["data"]["delete_mode"] = "soft_delete";

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}
