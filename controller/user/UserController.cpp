#include "controller/user/UserController.h"
#include "service/user/UserService.h" // 引入 Service 层
#include "entity/UserEntity.h"
#include <json/json.h>
#include <iostream>
#include <algorithm>
#include <cctype>

// User模块的路由注册(使用router)
void UserController::initRoutes(Router *router)
{
    // Controller使用Router注册路由:"POST"+"/api/user/add"-->UserController::handleAddUser
    router->addRoute("POST", "/api/user/add", UserController::handleAddUser);

    // 注册路由:"POST"+"/api/user/login"-->UserController::handleLogin
    router->addRoute("POST", "/api/user/login", UserController::handleLogin);

    // 注册路由:"GET"+"/api/user/info"-->UserController::handleGetUserInfo
    router->addRoute("GET", "/api/user/info", UserController::handleGetUserInfo);

    // 注册路由:"POST"+"/api/user/update"-->UserController::handleUpdateUser
    router->addRoute("POST", "/api/user/update", UserController::handleUpdateUser);
}

// 负责"/api/user/add"的接口入口函数
void UserController::handleAddUser(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(req.body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
        return;
    }

    UserEntity user;
    user.username = root["username"].asString();
    user.password = root["password"].asString();
    user.nickname = root["nickname"].asString();
    user.employee_id = root["employee_id"].asString();
    user.email = root["email"].asString();
    user.phone = root["phone"].asString();
    user.department = root["department"].asString();
    user.location = root["location"].asString();
    user.timezone = root["timezone"].asString();
    user.bio = root["bio"].asString();
    user.role = root["role"].asString();

    // ===============校验参数阶段===============

    // 1. 校验用户名：只能是 英文 和 数字
    // std::all_of 会遍历字符串每个字符，::isalnum 检查字符是否为字母或数字
    if (user.username.empty() || !std::all_of(user.username.begin(), user.username.end(), ::isalnum))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "用户名不能为空，且只能包含英文和数字"})";
        return;
    }

    // 2. 校验工号：必须是 3 位数字
    // ::isdigit 检查字符是否为 0-9 的数字
    if (user.employee_id.length() != 3 || !std::all_of(user.employee_id.begin(), user.employee_id.end(), ::isdigit))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "工号必须是3位纯数字"})";
        return;
    }

    // 3. 校验手机号：必须是 11 位数字
    if (user.phone.length() != 11 || !std::all_of(user.phone.begin(), user.phone.end(), ::isdigit))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "联系电话必须是11位纯数字"})";
        return;
    }

    // ===================================================================

    // 调用 Service 存库
    bool success = UserService::addUser(user);

    if (success)
    {
        res.statusCode = 200;
        res.body = R"({"code": 200, "msg": "添加用户成功"})";
    }
    else
    {
        res.statusCode = 500;
        // 注意：如果填了相同的 username 或 employee_id，这里会返回失败，因为数据库设置了 UNIQUE
        res.body = R"({"code": 500, "msg": "添加失败，可能是工号或用户名已存在"})";
    }
}

// 负责"/api/user/login"的接口入口函数
void UserController::handleLogin(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(req.body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code":400, "msg":"JSON错误"})";
        return;
    }

    std::string username = root["username"].asString();
    std::string password = root["password"].asString();

    UserEntity user;
    // 核心调用：执行登录验证
    if (UserService::login(username, password, user))
    {
        res.statusCode = 200;
        // 登录成功，给前端返回用户信息（包含状态）
        res.body = R"({
            "code": 200, 
            "msg": "登录成功", 
            "data": {
                "id": )" +
                   std::to_string(user.id) + R"(,
                "username": ")" +
                   user.username + R"(",
                "role": ")" +
                   user.role + R"("
            }
        })";
    }
    else
    {
        res.statusCode = 401; // 401 Unauthorized
        res.body = R"({"code": 401, "msg": "用户名不存在或密码错误"})";
    }
}

// 负责"/api/user/info"的接口入口函数
void UserController::handleGetUserInfo(const HttpRequest &req, HttpResponse &res)
{
    // 不再解析 Body，而是直接从 queryParams 字典里拿 username
    auto it = req.queryParams.find("username");

    // 校验参数是否存在
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少 username 参数"})";
        return;
    }

    std::string username = it->second;

    UserEntity user;
    if (UserService::getUserInfo(username, user))
    {
        // 【高端操作】：使用 jsoncpp 组装复杂的返回结构，绝对不会出错！
        Json::Value res_root;
        res_root["code"] = 200;
        res_root["msg"] = "获取用户信息成功";

        Json::Value data;
        // 把除了 id 和 password 之外的所有字段塞进去
        data["username"] = user.username;
        data["nickname"] = user.nickname;
        data["employee_id"] = user.employee_id;
        data["email"] = user.email;
        data["phone"] = user.phone;
        data["department"] = user.department;
        data["location"] = user.location;
        data["timezone"] = user.timezone;
        data["role"] = user.role;
        data["bio"] = user.bio;
        data["last_login"] = user.last_login;

        res_root["data"] = data;

        // 将 JSON 对象转成字符串返回
        Json::FastWriter writer;
        res.body = writer.write(res_root);
        res.statusCode = 200;
    }
    else
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "用户不存在"})";
    }
}

// 负责"/api/user/update"的接口入口函数
void UserController::handleUpdateUser(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(req.body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
        return;
    }

    UserEntity user;
    // 必须有 username，才知道要更新谁
    user.username = root["username"].asString();

    if (user.username.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺失必填字段: username"})";
        return;
    }

    // 解析其他要更新的字段 (没有 password)
    user.nickname = root["nickname"].asString();
    user.employee_id = root["employee_id"].asString();
    user.email = root["email"].asString();
    user.phone = root["phone"].asString();
    user.department = root["department"].asString();
    user.location = root["location"].asString();
    user.timezone = root["timezone"].asString();
    user.bio = root["bio"].asString();
    user.role = root["role"].asString();

    // =============== 【参数严格校验阶段 (复用之前的逻辑)】 ===============

    // 校验工号：必须是 3 位数字
    if (user.employee_id.length() != 3 || !std::all_of(user.employee_id.begin(), user.employee_id.end(), ::isdigit))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "工号必须是3位纯数字"})";
        return;
    }

    // 校验手机号：必须是 11 位数字
    if (user.phone.length() != 11 || !std::all_of(user.phone.begin(), user.phone.end(), ::isdigit))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "联系电话必须是11位纯数字"})";
        return;
    }
    // ===================================================================

    // 调用 Service 存库
    bool success = UserService::updateUserInfo(user);

    if (success)
    {
        res.statusCode = 200;
        res.body = R"({"code": 200, "msg": "修改个人信息成功"})";
    }
    else
    {
        res.statusCode = 500;
        res.body = R"({"code": 500, "msg": "修改失败，请检查工号是否已被其他人占用"})";
    }
}