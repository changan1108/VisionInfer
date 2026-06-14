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

// 获取连接池
MysqlPool &MysqlPool::instance()
{
    // 单例模式(保证整个进程只有一个连接池)
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
    std::lock_guard<std::mutex> lock(mutex_);// 先加锁

    // 已经初始化的话 就退出
    if (initialized_)
    {
        return;
    }

    // 检查
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

    // 创建最小连接数
    for (std::size_t i = 0; i < min_size_; ++i)
    {
        // 每次创建出一个"单条连接"，都放入空闲队列
        idle_connections_.push(createConnectionLocked());
    }

    initialized_ = true;
}

// 从空闲队列中借出一个"空闲sql连接"(包装成BorrowedConn)
MysqlPool::BorrowedConn MysqlPool::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);// 加锁
    // 保证先进行了初始化
    if (!initialized_)
    {
        throw std::runtime_error("mysql pool not initialized");
    }

    // 情况三：没有空闲连接，并且达到上限
    while (idle_connections_.empty() && total_size_ >= max_size_)
    {
        // 释放互斥锁+暂停当前线程+被通知后继续加锁(同时会跳出while)
        condition_.wait(lock);
    }

    // 情况一：存在空闲连接
    if (!idle_connections_.empty())
    {
        // 从队首取出一个
        std::shared_ptr<MysqlConn> conn = idle_connections_.front();
        idle_connections_.pop();

        // 包装成BorrowedConn对象
        return BorrowedConn(this, std::move(conn));
    }

    // 情况二：没有空闲连接，但没有达到上限
    // 这时会新建新的一条sql连接
    return BorrowedConn(this, createConnectionLocked());
}

// 创建单条mysql连接
std::shared_ptr<MysqlConn> MysqlPool::createConnectionLocked()
{
    // 实例化该类对象
    std::shared_ptr<MysqlConn> conn = std::make_shared<MysqlConn>();

    // mysql单条连接对象调用mysql API，与MYSQL服务建立连接
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
