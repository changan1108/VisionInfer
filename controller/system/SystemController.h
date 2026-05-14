#ifndef SYSTEM_CONTROLLER_H
#define SYSTEM_CONTROLLER_H

#include "controller/network/Router.h"
#include "entity/HttpEntity.h"

class SystemController
{
public:
    static void initRoutes(Router *router);

private:
    static void handleHeartbeat(const HttpRequest &req, HttpResponse &res);
    static void handleGetSystemStatus(const HttpRequest &req, HttpResponse &res);
    static void handleGetSystemOverview(const HttpRequest &req, HttpResponse &res);
};

#endif // SYSTEM_CONTROLLER_H
