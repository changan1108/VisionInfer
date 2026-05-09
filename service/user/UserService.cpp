#include "service/user/UserService.h"
#include "dao/user/UserDao.h" // 引入 DAO 层
#include <iostream>

bool UserService::addUser(const UserEntity &user)
{
    // 1. 业务逻辑校验：比如检查账号密码是否为空
    if (user.username.empty() || user.password.empty())
    {
        std::cerr << "[UserService WARNING] 注册失败：用户名或密码不能为空！" << std::endl;
        return false;
    }

    std::cout << "[UserService INFO] 业务校验通过，准备交由 DAO 层存入数据库..." << std::endl;

    // 2. 调用 DAO 层，将数据持久化
    return UserDao::insertUser(user);
}

bool UserService::login(const std::string &username, const std::string &password, UserEntity &out_user)
{
    // 1. 去数据库查这个用户名存不存在
    if (!UserDao::getUserByUsername(username, out_user))
    {
        std::cerr << "[UserService] 登录失败：用户不存在!" << std::endl;
        return false;
    }

    // 2. 比对密码
    if (out_user.password != password)
    {
        std::cerr << "[UserService] 登录失败：密码错误!" << std::endl;
        return false;
    }

    // 3. 密码正确，刷新最后登录时间
    UserDao::updateLoginState(out_user.id);

    // (可选) 重新查一次把带 last_login 的最新数据捞出来，这里为简化直接返回
    std::cout << "[UserService] 用户 " << username << " 登录成功!" << std::endl;
    return true;
}

// 业务层：获取用户详细信息
bool UserService::getUserInfo(const std::string &username, UserEntity &out_user)
{
    // 业务层直接调用 DAO 去数据库捞数据即可
    // 以后如果需要判断“用户是否被封号”等逻辑，也可以写在这里
    if (UserDao::getUserByUsername(username, out_user))
    {
        return true;
    }
    return false;
}

// 更新用户信息
bool UserService::updateUserInfo(const UserEntity &user)
{
    if (user.username.empty())
    {
        std::cerr << "[UserService ERROR] 更新失败：用户名为空！" << std::endl;
        return false;
    }
    // 直接调用 DAO 执行更新
    return UserDao::updateUser(user);
}