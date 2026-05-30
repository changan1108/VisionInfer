#include "controller/network/Router.h"
#include <iostream>

// 用于向“路由表”添加路由项(即，注册路由的工具)
void Router::addRoute(const std::string &method, const std::string &path, HandlerFunc handler)
{
    std::string key = method + " " + path; // 拼接成字典的 Key
    routes_[key] = handler;
    std::cout << "[Router INFO] 成功挂载路由: " << key << std::endl;
}

// 用于查询当前的“路由表”，根据url查对应的注册函数
void Router::handleRequest(const HttpRequest &req, HttpResponse &res)
{
    std::string key = req.method + " " + req.path;

    // 在哈希表中极速查找
    auto it = routes_.find(key);
    if (it != routes_.end())
    {
        // 找到了对应的回调函数，执行
        // it指向map元素，即pair<string, HandlerFunc>，
        // 所以，it->first是URL（如"POST /api/user/login"）；it->second是函数签名HandlerFunc,本质是可调用对象（如UserController::handleLogin）
        it->second(req, res);// 可调用对象(函数签名)+"()"+实参
    }
    else
    {
        // 没找到路由，说明前端发错了路径，返回经典 404
        std::cerr << "[Router WARNING] 找不到请求的路由: " << key << std::endl;
        res.statusCode = 404;
        res.body = R"({"error": "404 Not Found", "msg": "接口不存在"})";
    }
}