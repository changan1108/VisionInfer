#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include "dao/db_conn/MysqlConn.h"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

class MysqlPool
{
public:
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

        MysqlConn *operator->() const;
        MysqlConn &operator*() const;
        bool valid() const;

    private:
        void release();

        MysqlPool *pool_;
        std::shared_ptr<MysqlConn> conn_;
    };

    static MysqlPool &instance();

    void initialize(const std::string &user,
                    const std::string &passwd,
                    const std::string &db_name,
                    const std::string &host,
                    unsigned int port,
                    std::size_t min_size,
                    std::size_t max_size);

    BorrowedConn acquire();

private:
    MysqlPool();

    std::shared_ptr<MysqlConn> createConnectionLocked();
    void release(std::shared_ptr<MysqlConn> conn);

    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::shared_ptr<MysqlConn>> idle_connections_;
    std::string user_;
    std::string passwd_;
    std::string db_name_;
    std::string host_;
    unsigned int port_;
    std::size_t min_size_;
    std::size_t max_size_;
    std::size_t total_size_;
    bool initialized_;
};

#endif // MYSQL_POOL_H
