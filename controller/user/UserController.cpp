#include "controller/user/UserController.h"
#include "service/auth/AuthService.h"
#include "service/user/UserService.h" // 引入 Service 层
#include "entity/UserEntity.h"
#include <json/json.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace
{
bool parseJsonObjectBody(const std::string &body, Json::Value &root, HttpResponse &res)
{
    Json::Reader reader;
    if (!reader.parse(body, root))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 格式错误"})";
        return false;
    }

    if (!root.isObject())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "JSON 请求体必须是对象"})";
        return false;
    }

    return true;
}

bool isDigitsOnly(const std::string &value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char ch)
                       { return std::isdigit(ch) != 0; });
}

bool validateContactFields(const UserEntity &user, HttpResponse &res)
{
    if (user.phone.length() != 11 || !isDigitsOnly(user.phone))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "联系电话必须是11位纯数字"})";
        return false;
    }

    if (user.email.find('@') == std::string::npos)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "电子邮箱必须包含@"})";
        return false;
    }

    return true;
}

bool parseIntParam(const std::string &value, int &out_value)
{
    try
    {
        std::size_t parsed = 0;
        int parsed_value = std::stoi(value, &parsed);
        if (parsed != value.size())
        {
            return false;
        }
        out_value = parsed_value;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int authErrorStatusCode(const std::string &error_message)
{
    if (error_message.find("禁用") != std::string::npos ||
        error_message.find("权限等级非法") != std::string::npos)
    {
        return 403;
    }
    return 401;
}

Json::Value buildUserListItemJson(const UserEntity &user)
{
    Json::Value item;
    item["id"] = user.id;
    item["username"] = user.username;
    item["nickname"] = user.nickname;
    item["employee_id"] = user.employee_id;
    item["email"] = user.email;
    item["phone"] = user.phone;
    item["department"] = user.department;
    item["location"] = user.location;
    item["timezone"] = user.timezone;
    item["role"] = user.role;
    item["permission_level"] = user.permission_level;
    item["account_status"] = user.account_status;
    item["last_login"] = user.last_login;
    item["created_at"] = user.created_at;
    item["updated_at"] = user.updated_at;
    return item;
}
}

// User模块的路由注册(使用router)
void UserController::initRoutes(Router *router)
{
    // Controller使用Router注册路由:"POST"+"/api/user/add"-->UserController::handleAddUser
    router->addRoute("POST", "/api/user/add", UserController::handleAddUser);

    // 注册路由:"POST"+"/api/user/login"-->UserController::handleLogin
    router->addRoute("POST", "/api/user/login", UserController::handleLogin);

    // 注册路由:"GET"+"/api/user/info"-->UserController::handleGetUserInfo
    router->addRoute("GET", "/api/user/info", UserController::handleGetUserInfo);

    // 注册路由:"GET"+"/api/user/list"-->UserController::handleListUsers
    router->addRoute("GET", "/api/user/list", UserController::handleListUsers);

    // 注册路由:"POST"+"/api/user/update"-->UserController::handleUpdateUser
    router->addRoute("POST", "/api/user/update", UserController::handleUpdateUser);

    // 注册路由:"POST"+"/api/user/change-password"-->UserController::handleChangePassword
    router->addRoute("POST", "/api/user/change-password", UserController::handleChangePassword);

    router->addRoute("DELETE", "/api/user", UserController::handleDeleteUser);
}

// 负责"/api/user/add"的接口入口函数
void UserController::handleAddUser(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    if (!parseJsonObjectBody(req.body, root, res))
    {
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
    if (user.username.empty() || !std::all_of(user.username.begin(), user.username.end(), [](unsigned char ch)
                                              { return std::isalnum(ch) != 0; }))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "用户名不能为空，且只能包含英文和数字"})";
        return;
    }

    // 2. 校验工号：必须是 3 位数字
    // ::isdigit 检查字符是否为 0-9 的数字
    if (user.employee_id.length() != 3 || !isDigitsOnly(user.employee_id))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "工号必须是3位纯数字"})";
        return;
    }

    // 3. 校验联系方式
    if (!validateContactFields(user, res))
    {
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
    if (!parseJsonObjectBody(req.body, root, res))
    {
        return;
    }

    std::string username = root["username"].asString();
    std::string password = root["password"].asString();

    UserEntity user;
    // 核心调用：执行登录验证
    if (UserService::login(username, password, user))
    {
        Json::Value response;
        response["code"] = 200;
        response["msg"] = "登录成功";
        response["data"]["id"] = user.id;
        response["data"]["username"] = user.username;
        response["data"]["role"] = user.role;
        response["data"]["permission_level"] = user.permission_level;
        response["data"]["account_status"] = user.account_status;

        Json::FastWriter writer;
        res.statusCode = 200;
        res.body = writer.write(response);
    }
    else
    {
        res.statusCode = 401; // 401 Unauthorized
        res.body = R"({"code": 401, "msg": "用户名不存在、密码错误或账号已被禁用"})";
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
        data["permission_level"] = user.permission_level;
        data["account_status"] = user.account_status;
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

void UserController::handleListUsers(const HttpRequest &req, HttpResponse &res)
{
    OperatorContext operator_context;
    std::string auth_error;
    if (!AuthService::getOperatorContext(req, operator_context, auth_error))
    {
        res.statusCode = authErrorStatusCode(auth_error);
        res.body = std::string("{\"code\": ") + std::to_string(res.statusCode) + ", \"msg\": \"" + auth_error + "\"}";
        return;
    }

    if (!operator_context.is_admin)
    {
        res.statusCode = 403;
        res.body = R"({"code": 403, "msg": "权限不足，仅管理员可查看用户列表"})";
        return;
    }

    UserListFilter filter;
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("username");
    if (it != req.queryParams.end())
    {
        filter.username = it->second;
    }

    it = req.queryParams.find("employee_id");
    if (it != req.queryParams.end())
    {
        filter.employee_id = it->second;
    }

    it = req.queryParams.find("status");
    if (it != req.queryParams.end() && !it->second.empty())
    {
        if (it->second != "active" && it->second != "disabled")
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "status 仅支持 active 或 disabled"})";
            return;
        }
        filter.status = it->second;
    }

    it = req.queryParams.find("permission_level");
    if (it != req.queryParams.end() && !it->second.empty())
    {
        int permission_level = -1;
        if (!parseIntParam(it->second, permission_level) ||
            (permission_level != 0 && permission_level != 1))
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "permission_level 仅支持 0 或 1"})";
            return;
        }
        filter.permission_level = permission_level;
    }

    it = req.queryParams.find("limit");
    if (it != req.queryParams.end() && !it->second.empty())
    {
        if (!parseIntParam(it->second, filter.limit) || filter.limit < 1 || filter.limit > 100)
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "limit 取值范围必须在 1 到 100 之间"})";
            return;
        }
    }

    it = req.queryParams.find("offset");
    if (it != req.queryParams.end() && !it->second.empty())
    {
        if (!parseIntParam(it->second, filter.offset) || filter.offset < 0)
        {
            res.statusCode = 400;
            res.body = R"({"code": 400, "msg": "offset 必须大于等于 0"})";
            return;
        }
    }

    std::vector<UserEntity> users;
    int total = 0;
    if (!UserService::listUsers(filter, users, total))
    {
        res.statusCode = 500;
        res.body = R"({"code": 500, "msg": "获取用户列表失败"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "获取用户列表成功";
    response["data"]["total"] = total;
    response["data"]["limit"] = filter.limit;
    response["data"]["offset"] = filter.offset;
    response["data"]["items"] = Json::Value(Json::arrayValue);
    for (std::size_t i = 0; i < users.size(); ++i)
    {
        response["data"]["items"].append(buildUserListItemJson(users[i]));
    }

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}

// 负责"/api/user/update"的接口入口函数
void UserController::handleUpdateUser(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    if (!parseJsonObjectBody(req.body, root, res))
    {
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
    if (user.employee_id.length() != 3 || !isDigitsOnly(user.employee_id))
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "工号必须是3位纯数字"})";
        return;
    }

    // 校验联系方式
    if (!validateContactFields(user, res))
    {
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

void UserController::handleChangePassword(const HttpRequest &req, HttpResponse &res)
{
    Json::Value root;
    if (!parseJsonObjectBody(req.body, root, res))
    {
        return;
    }

    std::string username = root["username"].asString();
    std::string old_password = root["old_password"].asString();
    std::string new_password = root["new_password"].asString();

    if (username.empty() || old_password.empty() || new_password.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "username、old_password、new_password 为必填项"})";
        return;
    }

    std::string error_message;
    if (UserService::changePassword(username, old_password, new_password, error_message))
    {
        res.statusCode = 200;
        res.body = R"({"code": 200, "msg": "密码修改成功"})";
        return;
    }

    if (error_message == "账号不存在或已被禁用")
    {
        res.statusCode = 403;
    }
    else
    {
        res.statusCode = 400;
    }

    Json::Value response;
    response["code"] = res.statusCode;
    response["msg"] = error_message.empty() ? "密码修改失败" : error_message;

    Json::FastWriter writer;
    res.body = writer.write(response);
}

void UserController::handleDeleteUser(const HttpRequest &req, HttpResponse &res)
{
    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("username");
    if (it == req.queryParams.end() || it->second.empty())
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "缺少 username 参数"})";
        return;
    }

    std::string target_username = it->second;

    OperatorContext operator_context;
    std::string auth_error;
    if (!AuthService::getOperatorContext(req, operator_context, auth_error))
    {
        res.statusCode = authErrorStatusCode(auth_error);
        res.body = std::string("{\"code\": ") + std::to_string(res.statusCode) + ", \"msg\": \"" + auth_error + "\"}";
        return;
    }

    if (!operator_context.is_admin)
    {
        res.statusCode = 403;
        res.body = R"({"code": 403, "msg": "只有管理员可以禁用用户"})";
        return;
    }

    if (operator_context.username == target_username)
    {
        res.statusCode = 400;
        res.body = R"({"code": 400, "msg": "管理员不能禁用自己"})";
        return;
    }

    UserEntity target_user;
    if (!UserService::getUserInfo(target_username, target_user))
    {
        res.statusCode = 404;
        res.body = R"({"code": 404, "msg": "目标用户不存在"})";
        return;
    }

    if (!UserService::disableUser(target_username))
    {
        res.statusCode = 500;
        res.body = R"({"code": 500, "msg": "用户禁用失败"})";
        return;
    }

    Json::Value response;
    response["code"] = 200;
    response["msg"] = "用户已禁用";
    response["data"]["username"] = target_username;
    response["data"]["account_status"] = "disabled";

    Json::FastWriter writer;
    res.statusCode = 200;
    res.body = writer.write(response);
}
