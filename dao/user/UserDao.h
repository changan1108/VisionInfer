#ifndef USER_DAO_H
#define USER_DAO_H

#include "entity/UserEntity.h"

class UserDao
{
public:
    // 将用户数据插入数据库
    static bool insertUser(const UserEntity &user);

    // 按照用户名查询
    static bool getUserByUsername(const std::string &username, UserEntity &out_user);

    // 更新登录时间
    static bool updateLoginState(int user_id);

    // 更新用户信息
    static bool updateUser(const UserEntity &user);
};

#endif // USER_DAO_H