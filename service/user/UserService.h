#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include "dao/user/UserDao.h"
#include "entity/UserEntity.h"
#include <vector>

class UserService
{
public:
    // 业务层：处理添加用户的逻辑
    static bool addUser(const UserEntity &user);

    // 业务层：处理login的逻辑
    static bool login(const std::string &username, const std::string &password, UserEntity &out_user);

    // 业务层：获取用户详细信息
    static bool getUserInfo(const std::string &username, UserEntity &out_user);

    // 业务层：管理员查询用户列表
    static bool listUsers(const UserListFilter &filter, std::vector<UserEntity> &out_users, int &out_total);

    // 业务层：更新用户信息
    static bool updateUserInfo(const UserEntity &user);

    // 业务层：修改密码
    static bool changePassword(const std::string &username,
                               const std::string &old_password,
                               const std::string &new_password,
                               std::string &error_message);

    // 业务层：禁用用户
    static bool disableUser(const std::string &username);
};

#endif
