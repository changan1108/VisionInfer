#include "common/config/AppConfig.h"
#include "dao/db_conn/MysqlPool.h"
#include "dao/user/UserDao.h"
#include "dao/db_conn/MysqlConn.h" // 引入我们刚写的工具类
#include <iostream>

namespace
{
std::string escapeSql(const std::string &input)
{
    std::string escaped;
    escaped.reserve(input.size() * 2);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' || input[i] == '\'')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(input[i]);
    }
    return escaped;
}
}

// 插入新用户
bool UserDao::insertUser(const UserEntity &user)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    // 拼接超长 SQL (注意字段顺序)
    std::string sql = "INSERT INTO users (username, password, nickname, employee_id, email, phone, department, location, timezone, bio, role) VALUES ('" +
                      user.username + "', '" + user.password + "', '" +
                      user.nickname + "', '" + user.employee_id + "', '" +
                      user.email + "', '" + user.phone + "', '" +
                      user.department + "', '" + user.location + "', '" +
                      user.timezone + "', '" + user.bio + "', '" +
                      user.role + "');";

    return db->update(sql);
}

// 根据用户名获取用户信息
bool UserDao::getUserByUsername(const std::string &username, UserEntity &out_user)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT id, username, password, nickname, employee_id, email, phone, department, location, timezone, bio, role, last_login, permission_level, account_status FROM users WHERE username = '" + escapeSql(username) + "';";

    MYSQL_RES *res = db->query(sql);
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
            out_user.permission_level = row[13] ? std::stoi(row[13]) : 0;
            out_user.account_status = row[14] ? row[14] : "active";

            mysql_free_result(res);
            return true;
        }
        mysql_free_result(res);
    }
    return false;
}

bool UserDao::getUserPermissionInfo(const std::string &username, UserPermissionInfo &out_info)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT username, permission_level, account_status FROM users WHERE username = '" +
                      escapeSql(username) + "' LIMIT 1;";

    MYSQL_RES *res = db->query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == nullptr)
    {
        mysql_free_result(res);
        return false;
    }

    out_info.username = row[0] ? row[0] : "";
    out_info.permission_level = row[1] ? std::stoi(row[1]) : 0;
    out_info.account_status = row[2] ? row[2] : "";

    mysql_free_result(res);
    return !out_info.username.empty();
}

bool UserDao::listUsers(const UserListFilter &filter, std::vector<UserEntity> &out_users, int &out_total)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string where_clause = " WHERE 1 = 1";
    if (!filter.username.empty())
    {
        where_clause += " AND username LIKE '%" + escapeSql(filter.username) + "%'";
    }

    if (!filter.employee_id.empty())
    {
        where_clause += " AND employee_id LIKE '%" + escapeSql(filter.employee_id) + "%'";
    }

    if (!filter.status.empty())
    {
        where_clause += " AND account_status = '" + escapeSql(filter.status) + "'";
    }

    if (filter.permission_level == 0 || filter.permission_level == 1)
    {
        where_clause += " AND permission_level = " + std::to_string(filter.permission_level);
    }

    std::string count_sql = "SELECT COUNT(*) FROM users" + where_clause + ";";
    MYSQL_RES *count_res = db->query(count_sql);
    if (count_res == nullptr)
    {
        return false;
    }

    MYSQL_ROW count_row = mysql_fetch_row(count_res);
    if (count_row == nullptr)
    {
        mysql_free_result(count_res);
        return false;
    }
    out_total = count_row[0] ? std::stoi(count_row[0]) : 0;
    mysql_free_result(count_res);

    std::string sql =
        "SELECT id, username, nickname, employee_id, email, phone, department, location, timezone, role, "
        "permission_level, account_status, last_login, create_time "
        "FROM users" +
        where_clause +
        " ORDER BY id ASC LIMIT " + std::to_string(filter.limit) +
        " OFFSET " + std::to_string(filter.offset) + ";";

    MYSQL_RES *res = db->query(sql);
    if (res == nullptr)
    {
        return false;
    }

    out_users.clear();
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        UserEntity user;
        user.id = row[0] ? std::stoi(row[0]) : 0;
        user.username = row[1] ? row[1] : "";
        user.nickname = row[2] ? row[2] : "";
        user.employee_id = row[3] ? row[3] : "";
        user.email = row[4] ? row[4] : "";
        user.phone = row[5] ? row[5] : "";
        user.department = row[6] ? row[6] : "";
        user.location = row[7] ? row[7] : "";
        user.timezone = row[8] ? row[8] : "";
        user.role = row[9] ? row[9] : "";
        user.permission_level = row[10] ? std::stoi(row[10]) : 0;
        user.account_status = row[11] ? row[11] : "active";
        user.last_login = row[12] ? row[12] : "";
        user.created_at = row[13] ? row[13] : "";
        user.updated_at = "";
        out_users.push_back(user);
    }

    mysql_free_result(res);
    return true;
}

// 登录成功后，更新时间
bool UserDao::updateLoginState(int user_id)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();
    std::string sql = "UPDATE users SET last_login = NOW() WHERE id = " + std::to_string(user_id) + ";";
    return db->update(sql);
}

// 更新用户信息
bool UserDao::updateUser(const UserEntity &user)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

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
    return db->update(sql);
}

bool UserDao::updatePassword(const std::string &username, const std::string &new_password)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE users SET password = '" + escapeSql(new_password) +
                      "' WHERE username = '" + escapeSql(username) + "';";
    return db->update(sql);
}

bool UserDao::disableUser(const std::string &username)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE users SET account_status = 'disabled' WHERE username = '" +
                      escapeSql(username) + "';";
    return db->update(sql);
}
