#include <iostream>
#include "common/config/AppConfig.h"
#include "common/monitor/SystemMonitor.h"
#include "controller/network/Router.h"
#include "controller/network/EpollServer.h"

// 引入各个业务模块的 Controller
#include "controller/user/UserController.h"
#include "controller/video/VideoController.h"
// #include "controller/inference/ModelController.h" // 以后引入

int main()
{
    std::cout << "[INFO] 视觉推理系统后端启动中..." << std::endl;
    SystemMonitor::instance().markServerStarted();

    // 实例化路由分发器
    Router router;

    // 模块化路由注册：让各个 Controller 自己注册路由
    UserController::initRoutes(&router);
    VideoController::initRoutes(&router);
    // ModelController::initRoutes(&router);

    std::cout << "[INFO] 所有业务模块路由初始化完毕。" << std::endl;

    // 实例化底层 Epoll 服务器
    int port = AppConfig::SERVER_PORT;
    EpollServer server(port, &router);

    // 启动服务器监听
    std::cout << "[INFO] Epoll 服务器即将启动，监听端口: " << port << std::endl;
    server.start();

    return 0;
}
