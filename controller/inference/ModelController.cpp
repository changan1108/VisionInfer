#include "controller/inference/ModelController.h"

#include <json/json.h>
#include <vector>

#include "entity/ModelEntity.h"
#include "service/model/ModelService.h"

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
}

void ModelController::initRoutes(Router *router)
{
    router->addRoute("POST", "/api/model/add", ModelController::handleAddModel);
    router->addRoute("POST", "/api/model/switch", ModelController::handleSwitchModel);
    router->addRoute("GET", "/api/model/current", ModelController::handleGetCurrentModel);
    router->addRoute("GET", "/api/model/list", ModelController::handleListModels);
}

void ModelController::handleAddModel(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(req.body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
        return;
    }

    ModelEntity model;
    model.model_name = root["model_name"].asString();
    model.file_path = root["file_path"].asString();
    model.framework = root.isMember("framework") ? root["framework"].asString() : "onnx";
    model.uploaded_by = root.isMember("uploaded_by") ? root["uploaded_by"].asString() : "";
    model.is_active = root.isMember("is_active") ? root["is_active"].asBool() : false;

    std::string error_message;
    if (!ModelService::addModel(model, error_message))
    {
        res.statusCode = 400;
        res.body = std::string("{\"code\": 400, \"msg\": \"") + error_message + "\"}";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "模型新增成功";
    response["data"] = buildModelJson(model);

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

void ModelController::handleSwitchModel(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(req.body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
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
