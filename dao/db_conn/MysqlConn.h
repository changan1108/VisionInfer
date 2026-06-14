#ifndef MYSQL_CONN_H
#define MYSQL_CONN_H

#include <mysql/mysql.h>
#include <string>

// 单条MYSQL连接类(其对象是某条MYSQL连接)
class MysqlConn
{
public:
    // 构造函数：初始化连接对象
    MysqlConn();
    // 析构函数：释放连接
    ~MysqlConn();

    // 连接数据库
    bool connect(const std::string &user, const std::string &passwd,
                 const std::string &dbName, const std::string &ip = "127.0.0.1",
                 unsigned int port = 3306);

    // 执行更新语句 (Insert, Update, Delete)
    bool update(const std::string &sql);

    // 执行查询语句 (Select)
    MYSQL_RES *query(const std::string &sql);

    // 获取刚刚插入的记录的主键
    long long getLastInsertId() const;

private:
    MYSQL *conn_; // MySQL 连接句柄
};

#endif
