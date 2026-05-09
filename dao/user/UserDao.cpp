#include "common/config/AppConfig.h"
#include "dao/user/UserDao.h"
#include "dao/db_conn/MysqlConn.h" // 引入我们刚写的工具类
#include <iostream>

// 插入新用户
bool UserDao::insertUser(const UserEntity &user)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
        return false;

    // 拼接超长 SQL (注意字段顺序)
    std::string sql = "INSERT INTO users (username, password, nickname, employee_id, email, phone, department, location, timezone, bio, role) VALUES ('" +
                      user.username + "', '" + user.password + "', '" +
                      user.nickname + "', '" + user.employee_id + "', '" +
                      user.email + "', '" + user.phone + "', '" +
                      user.department + "', '" + user.location + "', '" +
                      user.timezone + "', '" + user.bio + "', '" +
                      user.role + "');";

    return db.update(sql);
}

// 根据用户名获取用户信息
bool UserDao::getUserByUsername(const std::string &username, UserEntity &out_user)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
        return false;

    std::string sql = "SELECT id, username, password, nickname, employee_id, email, phone, department, location, timezone, bio, role, last_login FROM users WHERE username = '" + username + "';";

    MYSQL_RES *res = db.query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row != nullptr)
        {
            out_user.id = std::stoi(row[0]);
            out_user.username = row[1];
            out_user.password = row[2];
            out_user.nickname = row[3] ? row[3] : "";
            out_user.employee_id = row[4] ? row[4] : "";
            out_user.email = row[5] ? row[5] : "";
            out_user.phone = row[6] ? row[6] : "";
            out_user.department = row[7] ? row[7] : "";
            out_user.location = row[8] ? row[8] : "";
            out_user.timezone = row[9] ? row[9] : "";
            out_user.bio = row[10] ? row[10] : "";
            out_user.role = row[11] ? row[11] : "";
            // 抓取第 13 个字段 (索引 12) 作为上次登录时间
            out_user.last_login = row[12] ? row[12] : "";

            mysql_free_result(res);
            return true;
        }
        mysql_free_result(res);
    }
    return false;
}

// 登录成功后，更新时间
bool UserDao::updateLoginState(int user_id)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
        return false;
    std::string sql = "UPDATE users SET last_login = NOW() WHERE id = " + std::to_string(user_id) + ";";
    return db.update(sql);
}

// 更新用户信息
bool UserDao::updateUser(const UserEntity &user)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
        return false;

    // 拼接 UPDATE 语句，注意最后要有 WHERE 条件，否则会把全库的人都改了！
    std::string sql = "UPDATE users SET "
                      "nickname = '" +
                      user.nickname + "', "
                                      "employee_id = '" +
                      user.employee_id + "', "
                                         "email = '" +
                      user.email + "', "
                                   "phone = '" +
                      user.phone + "', "
                                   "department = '" +
                      user.department + "', "
                                        "location = '" +
                      user.location + "', "
                                      "timezone = '" +
                      user.timezone + "', "
                                      "bio = '" +
                      user.bio + "', "
                                 "role = '" +
                      user.role + "' "
                                  "WHERE username = '" +
                      user.username + "';";

    std::cout << "[UserDao INFO] 执行更新 SQL: " << sql << std::endl;
    return db.update(sql);
}
