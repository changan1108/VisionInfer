#ifndef USER_ENTITY_H
#define USER_ENTITY_H

#include <string>

struct UserEntity
{
    int id;
    std::string username;
    std::string password;
    std::string last_login;  // 上次登录时间
    std::string created_at;   // 创建时间
    std::string updated_at;   // 更新时间
    std::string nickname;    // 姓名/昵称
    std::string employee_id; // 工号(三位数字)
    std::string email;       // 电子邮箱
    std::string phone;       // 联系电话(11位)
    std::string department;  // 所属中心
    std::string location;    // 地理位置
    std::string timezone;    // 时区
    std::string bio;         // 个人简介
    std::string role;        // 职能角色
    int permission_level = 0; // 权限等级：0=普通操作员，1=管理员
    std::string account_status = "active"; // 账号状态：active/disabled
};

#endif // USER_ENTITY_H
