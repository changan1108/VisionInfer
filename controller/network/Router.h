#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <unordered_map>
#include <functional>
#include "entity/HttpEntity.h"

class Router
{
public:
    // function<>是C++标准库提供的“可调用对象包装器”
    // 返回值为void、参数列表为const HttpRequest &, HttpResponse &的函数，即是function<void(const HttpRequest &, HttpResponse &)>类型，
    // 别名HandlerFunc类型（该类型对应的变量值，就是符合对应要求的"函数签名"（即，函数名，本质是"可调用对象"））
    // 注意，这里传入的函数：普通函数或者静态成员函数，因为静态的可以直接使用类名指示出函数，而非静态成员函数需要对象，通常用lambda包一层。
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