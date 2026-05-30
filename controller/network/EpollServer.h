#ifndef EPOLL_SERVER_H
#define EPOLL_SERVER_H

#include "controller/network/Router.h"

#include <string>
#include <unordered_map>

// epoll每轮拉取就绪事件的缓冲数组大小
#define MAX_EVENTS 1024

class EpollServer
{
public:
    // 构造函数：传入监听端口和路由分发器的指针
    EpollServer(int port, Router *router);

    // 析构函数：释放资源
    ~EpollServer();

    // 启动服务器的死循环，开始监听
    void start();

private:
    int port_;       // 监听的端口
    int listen_fd_;  // 服务器监听的 Socket 文件描述符
    int epoll_fd_;   // Epoll 实例的文件描述符
    Router *router_; // 指向路由分发器的指针
    std::unordered_map<int, std::string> request_buffers_;

    // 初始化网络设置 (Socket, Bind, Listen, Epoll_create)
    bool init();

    // 将某个cfd设置为非阻塞模式 (高并发必备)
    void setNonBlocking(int fd);

    // 处理新的客户端连接
    void handleNewConnection();

    // 处理已有客户端发来的数据
    void handleReadEvent(int client_fd);

    // 关闭客户端连接并回收统计信息
    void closeClient(int client_fd);

    // 判断当前缓冲区中的 HTTP 请求是否已经接收完整
    bool isRequestComplete(const std::string &buffer) const;

    // 确保整个 HTTP 响应完整发送
    bool sendAll(int client_fd, const std::string &data);
};

#endif // EPOLL_SERVER_H
