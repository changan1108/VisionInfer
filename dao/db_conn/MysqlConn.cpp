#include "dao/db_conn/MysqlConn.h"
#include <iostream>

MysqlConn::MysqlConn()
{
    // 初始化 MYSQL 内部结构体
    conn_ = mysql_init(nullptr);
    // 设置字符集，防止中文乱码
    mysql_set_character_set(conn_, "utf8mb4");
}

MysqlConn::~MysqlConn()
{
    if (conn_ != nullptr)
    {
        mysql_close(conn_);
    }
}

// 连接数据库
bool MysqlConn::connect(const std::string &user, const std::string &passwd,
                        const std::string &dbName, const std::string &ip,
                        unsigned int port)
{
    // 尝试连接
    MYSQL *p = mysql_real_connect(conn_, ip.c_str(), user.c_str(),
                                  passwd.c_str(), dbName.c_str(), port, nullptr, 0);
    if (p == nullptr)
    {
        std::cerr << "[MysqlConn ERROR] 数据库连接失败: " << mysql_error(conn_) << std::endl;
        return false;
    }
    std::cout << "[MysqlConn INFO] 成功连接到 MySQL 数据库: " << dbName << std::endl;
    return true;
}

// 执行更新语句 (Insert, Update, Delete)
bool MysqlConn::update(const std::string &sql)
{
    // 执行 SQL 语句。执行成功返回 0
    if (mysql_query(conn_, sql.c_str()) != 0)
    {
        std::cerr << "[MysqlConn ERROR] SQL 执行失败: " << mysql_error(conn_) << std::endl;
        return false;
    }
    return true;
}

// 实现 query 方法
MYSQL_RES *MysqlConn::query(const std::string &sql)
{
    if (mysql_query(conn_, sql.c_str()) != 0)
    {
        std::cerr << "[MysqlConn ERROR] 查询失败: " << mysql_error(conn_) << std::endl;
        return nullptr;
    }
    // 返回查询结果集
    return mysql_store_result(conn_);
}

// 获取刚刚插入的记录的主键
long long MysqlConn::getLastInsertId() const
{
    return static_cast<long long>(mysql_insert_id(conn_));
}
