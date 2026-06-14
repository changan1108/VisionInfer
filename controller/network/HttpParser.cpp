#include "controller/network/HttpParser.h"
#include <sstream>

namespace
{
std::string trim(const std::string &input)
{
    std::size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    std::size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}
}

// 解析前端发来的http报文
HttpRequest HttpParser::parseRequest(const std::string &raw_request)
{
    HttpRequest req;
    if (raw_request.empty())
        return req;

    // 1、找到HTTP头部的第一行，即请求行，eg "POST /api/user/add HTTP/1.1"
    size_t first_line_end = raw_request.find("\r\n"); // 寻找第一个回车换行符的位置
    if (first_line_end == std::string::npos)          // 查找失败
        return req;

    std::string first_line = raw_request.substr(0, first_line_end);
    std::istringstream iss(first_line); // 创建一个“字符串流”(类似cin，用于利用流特性跳过空格)

    // 从第一行中提取 Method (POST) 和 Path (/api/user/add)
    std::string raw_url;
    iss >> req.method >> raw_url; // 先把整个 /api/user/info?username=chen123 拿出来

    // 尝试寻找URL中的问号 '?'
    size_t question_mark_pos = raw_url.find('?');
    if (question_mark_pos != std::string::npos)
    {
        // 如果有问号，截取问号前面的作为真实的路由路径
        req.path = raw_url.substr(0, question_mark_pos);

        // 截取问号后面的作为查询参数字符串 (如 "username=chen123&age=20")
        std::string query_string = raw_url.substr(question_mark_pos + 1);

        // 用 '&' 符号进行切割
        std::istringstream query_stream(query_string);
        // 键值对变量
        std::string kv_pair;
        while (std::getline(query_stream, kv_pair, '&'))
        {
            // 每个循环步获取一个键值对字符串

            // 找到当前键值对字符串的'='字符的下标
            size_t equals_pos = kv_pair.find('=');
            if (equals_pos != std::string::npos)
            {
                // 用 '=' 符号切分键和值
                std::string key = kv_pair.substr(0, equals_pos);
                std::string value = kv_pair.substr(equals_pos + 1);
                req.queryParams[key] = value;
            }
        }
    }
    else
    {
        // 如果没有问号，直接当成路径
        req.path = raw_url;
    }

    // 2、解析请求头
    // 先找请求头结束位置
    size_t headers_end = raw_request.find("\r\n\r\n");
    if (headers_end != std::string::npos)
    {
        // first_line_end是请求行末尾\r\n中\r的位置，+2后变成第一条请求头 Host 的开头
        std::size_t line_start = first_line_end + 2;
        // 循环解析所有请求头(当前行开头还没有到达请求头结束位置)
        while (line_start < headers_end)
        {
            // 每个循环步处理一条请求头

            // 找到当前请求头行的末尾
            std::size_t line_end = raw_request.find("\r\n", line_start);
            if (line_end == std::string::npos || line_end > headers_end)
            {
                break;
            }

            // 截取当前请求行
            std::string line = raw_request.substr(line_start, line_end - line_start);
            // 找到中间的":"
            std::size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos)
            {
                
                std::string key = trim(line.substr(0, colon_pos));
                std::string value = trim(line.substr(colon_pos + 1));
                req.headers[key] = value;
            }

            line_start = line_end + 2;
        }
    }

    // 3、找到 HTTP 报文中请求头和Body的分界线（即空行，连续的两个回车换行 "\r\n\r\n"）
    size_t body_start = raw_request.find("\r\n\r\n");
    if (body_start != std::string::npos)
    {
        // 提取出纯粹的Body数据 (例如 JSON 字符串)
        req.body = raw_request.substr(body_start + 4);
    }

    return req;
}

std::string HttpParser::buildResponse(const HttpResponse &res)
{
    std::string response_str;

    // 1. 拼接状态行 (200为OK)
    if (res.statusCode == 200)
    {
        response_str += "HTTP/1.1 200 OK\r\n";
    }
    else
    {
        response_str += "HTTP/1.1 " + std::to_string(res.statusCode) + " Error\r\n";
    }

    // 2. 拼接必要的 Header，告诉前端我们返回的数据类型及长度
    response_str += "Content-Type: " + res.contentType + "\r\n";
    response_str += "Content-Length: " + std::to_string(res.body.length()) + "\r\n";
    if (!res.contentDisposition.empty())
    {
        response_str += "Content-Disposition: " + res.contentDisposition + "\r\n";
    }

    // 3. 跨域支持 (CORS)：允许前端跨域访问我们的接口
    response_str += "Access-Control-Allow-Origin: *\r\n";
    response_str += "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
    response_str += "Access-Control-Allow-Headers: Content-Type, Authorization, X-Operator-Username\r\n";
    response_str += "Connection: close\r\n";

    // 4. 空行，标志着 Header 的结束和 Body 的开始
    response_str += "\r\n";

    // 5. 拼装具体的业务数据
    response_str += res.body;

    return response_str;
}
