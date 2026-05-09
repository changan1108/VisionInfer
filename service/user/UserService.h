#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include "entity/UserEntity.h"

class UserService
{
public:
    // 业务层：处理添加用户的逻辑
    static bool addUser(const UserEntity &user);

    // 业务层：处理login的逻辑
    static bool login(const std::string &username, const std::string &password, UserEntity &out_user);

    // 业务层：获取用户详细信息
    static bool getUserInfo(const std::string &username, UserEntity &out_user);

    // 业务层：更新用户信息
    static bool updateUserInfo(const UserEntity &user);
};

#endif