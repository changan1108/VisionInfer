#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <unordered_map>
#include <functional>
#include "entity/HttpEntity.h"

class Router
{
public:
    // 定义处理函数的类型（输入请求对象，填充响应对象）
    using HandlerFunc = std::function<void(const HttpRequest &, HttpResponse &)>;

    // 开放给各个业务 Controller 使用的路由注册接口:用于向“路由表”添加路由项(即，注册路由的工具)
    void addRoute(const std::string &method, const std::string &path, HandlerFunc handler);

    // 用于查询当前的“路由表”，根据“请求方法 + url”查对应的注册函数，并调用执行
    void handleRequest(const HttpRequest &req, HttpResponse &res);

private:
    // 路由表字典：Key 是 "POST /api/user/add"，Value 是具体的业务函数
    std::unordered_map<std::string, HandlerFunc> routes_;
};

#endif // ROUTER_H