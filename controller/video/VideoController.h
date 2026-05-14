#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include "controller/network/Router.h"
#include "entity/HttpEntity.h"

class VideoController
{
public:
    static void initRoutes(Router *router);

private:
    static void handleUploadVideo(const HttpRequest &req, HttpResponse &res);
    static void handleSubmitTask(const HttpRequest &req, HttpResponse &res);
    static void handleGetTaskStatus(const HttpRequest &req, HttpResponse &res);
    static void handleListTasks(const HttpRequest &req, HttpResponse &res);
    static void handleGetTaskStats(const HttpRequest &req, HttpResponse &res);
    static void handleListVideos(const HttpRequest &req, HttpResponse &res);
    static void handleGetVideoInfo(const HttpRequest &req, HttpResponse &res);
    static void handlePreviewVideo(const HttpRequest &req, HttpResponse &res);
    static void handleGetResultVideo(const HttpRequest &req, HttpResponse &res);
};

#endif // VIDEO_CONTROLLER_H
