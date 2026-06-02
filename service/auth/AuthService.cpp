#include "service/auth/AuthService.h"

#include <algorithm>
#include <cctype>
#include <json/json.h>

#include "dao/user/UserDao.h"

namespace
{
constexpr int kOperatorPermissionLevel = 0;
constexpr int kAdminPermissionLevel = 1;

std::string toLowerCopy(const std::string &input)
{
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string getHeaderIgnoreCase(const HttpRequest &req, const std::string &target_key)
{
    std::string lowered_target = toLowerCopy(target_key);
    for (std::unordered_map<std::string, std::string>::const_iterator it = req.headers.begin();
         it != req.headers.end(); ++it)
    {
        if (toLowerCopy(it->first) == lowered_target)
        {
            return it->second;
        }
    }
    return "";
}

std::string getOperatorUsernameFromBody(const std::string &body)
{
    if (body.empty())
    {
        return "";
    }

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(body, root) || !root.isObject())
    {
        return "";
    }

    return root["operator_username"].asString();
}

std::string getOperatorUsername(const HttpRequest &req)
{
    std::string username = getHeaderIgnoreCase(req, "X-Operator-Username");
    if (!username.empty())
    {
        return username;
    }

    std::unordered_map<std::string, std::string>::const_iterator it = req.queryParams.find("operator_username");
    if (it != req.queryParams.end())
    {
        return it->second;
    }

    return getOperatorUsernameFromBody(req.body);
}
}

bool AuthService::getOperatorContext(const HttpRequest &req,
                                     OperatorContext &out_context,
                                     std::string &error_message)
{
    std::string username = getOperatorUsername(req);
    if (username.empty())
    {
        error_message = "缺少操作者身份，请提供 X-Operator-Username 或 operator_username";
        return false;
    }

    UserPermissionInfo permission_info;
    if (!UserDao::getUserPermissionInfo(username, permission_info))
    {
        error_message = "操作者不存在";
        return false;
    }

    if (permission_info.account_status != "active")
    {
        error_message = "操作者账号已被禁用";
        return false;
    }

    if (permission_info.permission_level != kOperatorPermissionLevel &&
        permission_info.permission_level != kAdminPermissionLevel)
    {
        error_message = "操作者权限等级非法";
        return false;
    }

    out_context.username = permission_info.username;
    out_context.permission_level = permission_info.permission_level;
    out_context.is_admin = permission_info.permission_level == kAdminPermissionLevel;
    error_message.clear();
    return true;
}

bool AuthService::canDeleteOwnedResource(const OperatorContext &context,
                                         const std::string &resource_owner)
{
    if (context.is_admin)
    {
        return true;
    }
    return !context.username.empty() && context.username == resource_owner;
}
