#ifndef _USERCONTROLER_H_
#define _USERCONTROLER_H_

#include "controller/network/Router.h"
#include "entity/HttpEntity.h"

class UserController
{
public:
    // User模块的路由注册(使用router)
    static void initRoutes(Router *router);

private:
    // 注册的入口函数：添加用户
    static void handleAddUser(const HttpRequest &req, HttpResponse &res);

    // 注册的入口函数：登录(根据用户名)
    static void handleLogin(const HttpRequest &req, HttpResponse &res);

    // 注册的入口函数：获取用户信息(根据用户名)
    static void handleGetUserInfo(const HttpRequest &req, HttpResponse &res);

    // 注册的入口函数：更新用户信息(根据用户名)
    static void handleUpdateUser(const HttpRequest &req, HttpResponse &res);
};

#endif // _USERCONTROLER_H_