#ifndef USER_DAO_H
#define USER_DAO_H

#include "entity/UserEntity.h"
#include <vector>

struct UserPermissionInfo
{
    std::string username;
    int permission_level = 0;
    std::string account_status;
};

struct UserListFilter
{
    std::string username;
    std::string employee_id;
    std::string status;
    int permission_level = -1;
    int limit = 20;
    int offset = 0;
};

class UserDao
{
public:
    // 将用户数据插入数据库
    static bool insertUser(const UserEntity &user);

    // 按照用户名查询
    static bool getUserByUsername(const std::string &username, UserEntity &out_user);

    // 查询权限字段，供 AuthService 使用
    static bool getUserPermissionInfo(const std::string &username, UserPermissionInfo &out_info);

    // 查询用户列表，供管理员用户管理页面使用
    static bool listUsers(const UserListFilter &filter, std::vector<UserEntity> &out_users, int &out_total);

    // 更新登录时间
    static bool updateLoginState(int user_id);

    // 更新用户信息
    static bool updateUser(const UserEntity &user);

    // 修改用户密码
    static bool updatePassword(const std::string &username, const std::string &new_password);

    // 禁用用户账号
    static bool disableUser(const std::string &username);
};

#endif // USER_DAO_H
