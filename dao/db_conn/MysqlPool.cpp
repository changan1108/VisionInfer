#include "dao/db_conn/MysqlPool.h"

#include <stdexcept>

MysqlPool::BorrowedConn::BorrowedConn()
    : pool_(nullptr)
{
}

MysqlPool::BorrowedConn::BorrowedConn(MysqlPool *pool, std::shared_ptr<MysqlConn> conn)
    : pool_(pool), conn_(std::move(conn))
{
}

MysqlPool::BorrowedConn::BorrowedConn(BorrowedConn &&other) noexcept
    : pool_(other.pool_), conn_(std::move(other.conn_))
{
    other.pool_ = nullptr;
}

MysqlPool::BorrowedConn &MysqlPool::BorrowedConn::operator=(BorrowedConn &&other) noexcept
{
    if (this != &other)
    {
        release();
        pool_ = other.pool_;
        conn_ = std::move(other.conn_);
        other.pool_ = nullptr;
    }
    return *this;
}

MysqlPool::BorrowedConn::~BorrowedConn()
{
    release();
}

MysqlConn *MysqlPool::BorrowedConn::operator->() const
{
    return conn_.get();
}

MysqlConn &MysqlPool::BorrowedConn::operator*() const
{
    return *conn_;
}

bool MysqlPool::BorrowedConn::valid() const
{
    return conn_ != nullptr;
}

void MysqlPool::BorrowedConn::release()
{
    if (pool_ != nullptr && conn_ != nullptr)
    {
        pool_->release(std::move(conn_));
    }
    pool_ = nullptr;
}

MysqlPool &MysqlPool::instance()
{
    static MysqlPool pool;
    return pool;
}

MysqlPool::MysqlPool()
    : port_(0),
      min_size_(0),
      max_size_(0),
      total_size_(0),
      initialized_(false)
{
}

void MysqlPool::initialize(const std::string &user,
                           const std::string &passwd,
                           const std::string &db_name,
                           const std::string &host,
                           unsigned int port,
                           std::size_t min_size,
                           std::size_t max_size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_)
    {
        return;
    }

    if (min_size == 0 || max_size == 0 || min_size > max_size)
    {
        throw std::invalid_argument("invalid mysql pool size configuration");
    }

    user_ = user;
    passwd_ = passwd;
    db_name_ = db_name;
    host_ = host;
    port_ = port;
    min_size_ = min_size;
    max_size_ = max_size;

    for (std::size_t i = 0; i < min_size_; ++i)
    {
        idle_connections_.push(createConnectionLocked());
    }

    initialized_ = true;
}

MysqlPool::BorrowedConn MysqlPool::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!initialized_)
    {
        throw std::runtime_error("mysql pool not initialized");
    }

    while (idle_connections_.empty() && total_size_ >= max_size_)
    {
        condition_.wait(lock);
    }

    if (!idle_connections_.empty())
    {
        std::shared_ptr<MysqlConn> conn = idle_connections_.front();
        idle_connections_.pop();
        return BorrowedConn(this, std::move(conn));
    }

    return BorrowedConn(this, createConnectionLocked());
}

std::shared_ptr<MysqlConn> MysqlPool::createConnectionLocked()
{
    std::shared_ptr<MysqlConn> conn = std::make_shared<MysqlConn>();
    if (!conn->connect(user_, passwd_, db_name_, host_, port_))
    {
        throw std::runtime_error("failed to create mysql pooled connection");
    }
    ++total_size_;
    return conn;
}

void MysqlPool::release(std::shared_ptr<MysqlConn> conn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    idle_connections_.push(std::move(conn));
    condition_.notify_one();
}
