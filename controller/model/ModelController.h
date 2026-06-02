#ifndef MODEL_CONTROLLER_H
#define MODEL_CONTROLLER_H

#include "controller/network/Router.h"
#include "entity/HttpEntity.h"

class ModelController
{
public:
    static void initRoutes(Router *router);

private:
    static void handleUploadModel(const HttpRequest &req, HttpResponse &res);
    static void handleSwitchModel(const HttpRequest &req, HttpResponse &res);
    static void handleGetCurrentModel(const HttpRequest &req, HttpResponse &res);
    static void handleListModels(const HttpRequest &req, HttpResponse &res);
    static void handleDeleteModel(const HttpRequest &req, HttpResponse &res);
};

#endif // MODEL_CONTROLLER_H
