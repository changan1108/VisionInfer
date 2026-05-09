#include "controller/network/EpollServer.h"
#include "controller/network/HttpParser.h"
#include "common/monitor/SystemMonitor.h"
#include "entity/HttpEntity.h"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

EpollServer::EpollServer(int port, Router *router)
    : port_(port), router_(router), listen_fd_(-1), epoll_fd_(-1)
{
    init();
}

EpollServer::~EpollServer()
{
    if (listen_fd_ != -1)
        close(listen_fd_);
    if (epoll_fd_ != -1)
        close(epoll_fd_);
}

void EpollServer::setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool EpollServer::init()
{
    // 创建 TCP Socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        std::cerr << "[EpollServer ERROR] 创建 Socket 失败!" << std::endl;
        return false;
    }

    // 设置端口复用 (防止重启服务器时报"端口被占用")
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定 IP 和 端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 使用本机服务器可用IP
    server_addr.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "[EpollServer ERROR] 绑定端口 " << port_ << " 失败!" << std::endl;
        return false;
    }

    // 开始监听
    if (listen(listen_fd_, SOMAXCONN) < 0)
    {
        std::cerr << "[EpollServer ERROR] 监听失败!" << std::endl;
        return false;
    }

    // 创建 Epoll红黑树 实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0)
    {
        std::cerr << "[EpollServer ERROR] 创建 Epoll 失败!" << std::endl;
        return false;
    }

    // 将监听 Socket 添加到 Epoll 中，关注"可读"事件 (EPOLLIN)
    struct epoll_event event;
    event.data.fd = listen_fd_;
    event.events = EPOLLIN; // 关注读事件
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);

    return true;
}

void EpollServer::start()
{
    struct epoll_event events[MAX_EVENTS];

    std::cout << "[EpollServer INFO] 服务器正式运行，等待客户端连接..." << std::endl;

    while (true)
    {
        // 阻塞等待事件发生，-1 表示无限等待
        int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);

        for (int i = 0; i < num_events; ++i)
        {
            int current_fd = events[i].data.fd;

            if (current_fd == listen_fd_)
            {
                // 有新的客户端请求链接进来
                handleNewConnection();
            }
            else if (events[i].events & EPOLLIN)
            {
                // 已经连接的客户端发来了数据
                handleReadEvent(current_fd);
            }
        }
    }
}

void EpollServer::handleNewConnection()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 接受新连接
    int client_fd = accept(listen_fd_, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0)
        return;

    // 将新客户设置为非阻塞
    setNonBlocking(client_fd);

    // 把新客户的 Socket 挂到 Epoll 上监听
    struct epoll_event event;
    event.data.fd = client_fd;
    event.events = EPOLLIN | EPOLLET; // 采用边缘触发模式 (ET)
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);

    SystemMonitor::instance().incrementConnections();
    std::cout << "[EpollServer] 新客户端上线，FD: " << client_fd << std::endl;
}

void EpollServer::handleReadEvent(int client_fd)
{
    std::string raw_request;
    char buffer[4096];

    for (;;)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read > 0)
        {
            raw_request.append(buffer, bytes_read);
            continue;
        }

        if (bytes_read == 0)
        {
            std::cout << "[EpollServer] 客户端下线，FD: " << client_fd << std::endl;
            closeClient(client_fd);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }

        std::cerr << "[EpollServer ERROR] 读取客户端数据失败，FD: " << client_fd << std::endl;
        closeClient(client_fd);
        return;
    }

    if (raw_request.empty())
    {
        return;
    }

    SystemMonitor::instance().incrementTotalRequests();

    // 核心链路：解析 -> 分发处理 -> 打包响应
    HttpRequest req = HttpParser::parseRequest(raw_request);
    HttpResponse res;

    if (req.method == "OPTIONS")
    {
        res.statusCode = 200;
        res.body = R"({"code": 200, "msg": "preflight ok"})";
    }
    else
    {
        // router根据路由表分发给 Controller 业务层处理
        router_->handleRequest(req, res);
    }

    // Http解析模块将结果打包为 HTTP 字符串
    std::string raw_response = HttpParser::buildResponse(res);

    // 将数据发送给客户端
    send(client_fd, raw_response.c_str(), raw_response.length(), 0);

    // 当前先保持短连接，业务处理完成后立即关闭
    closeClient(client_fd);
}

void EpollServer::closeClient(int client_fd)
{
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
    SystemMonitor::instance().decrementConnections();
}
