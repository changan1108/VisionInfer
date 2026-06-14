#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include "dao/db_conn/MysqlConn.h"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

// 数据库连接池类(负责管理多条 MysqlConn)
class MysqlPool
{
public:

    // 在MysqlPool所属域内再定义一个类
    // 被借出的mysql连接(将mysql单条连接包装了一下)
    class BorrowedConn
    {
    public:
        BorrowedConn();
        BorrowedConn(MysqlPool *pool, std::shared_ptr<MysqlConn> conn);
        BorrowedConn(const BorrowedConn &) = delete;
        BorrowedConn &operator=(const BorrowedConn &) = delete;
        BorrowedConn(BorrowedConn &&other) noexcept;
        BorrowedConn &operator=(BorrowedConn &&other) noexcept;
        ~BorrowedConn();

        // 重载operator->()
        MysqlConn *operator->() const;
        MysqlConn &operator*() const;
        bool valid() const;

    private:
        void release();

        MysqlPool *pool_;
        std::shared_ptr<MysqlConn> conn_;
    };

    // 获取连接池
    static MysqlPool &instance();

    // 连接池初始化
    void initialize(const std::string &user,
                    const std::string &passwd,
                    const std::string &db_name,
                    const std::string &host,
                    unsigned int port,
                    std::size_t min_size,
                    std::size_t max_size);

    // 从空闲队列中借出一个"空闲sql连接"(包装成BorrowedConn)
    BorrowedConn acquire();

private:
    MysqlPool();

    // 创建单条mysql连接
    std::shared_ptr<MysqlConn> createConnectionLocked();
    void release(std::shared_ptr<MysqlConn> conn);

    std::mutex mutex_;// 保护连接队列
    std::condition_variable condition_;// 条件变量，没有可用连接时，让线程等待
    std::queue<std::shared_ptr<MysqlConn>> idle_connections_; // 空闲队列:保存当前空闲的sql连接
    std::string user_;
    std::string passwd_;
    std::string db_name_;
    std::string host_;
    unsigned int port_;
    std::size_t min_size_;// 启动时创建的最小连接数
    std::size_t max_size_;// 最多允许创建的连接数
    std::size_t total_size_;// 当前实际创建的连接总数
    bool initialized_;// 连接池是否初始化
};

#endif // MYSQL_POOL_H
