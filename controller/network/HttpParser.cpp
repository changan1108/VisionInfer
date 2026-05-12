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

HttpRequest HttpParser::parseRequest(const std::string &raw_request)
{
    HttpRequest req;
    if (raw_request.empty())
        return req;

    // 1. 找到 HTTP 头部的第一行 (Request Line)，例如 "POST /api/user/add HTTP/1.1"
    size_t first_line_end = raw_request.find("\r\n"); // 寻找第一个回车换行符的位置
    if (first_line_end == std::string::npos)          // 查找失败
        return req;

    std::string first_line = raw_request.substr(0, first_line_end);
    std::istringstream iss(first_line); // 创建一个“字符串流”(类似cin，用于利用流特性跳过空格)

    // 从第一行中提取 Method (POST) 和 Path (/api/user/add)
    std::string raw_url;
    iss >> req.method >> raw_url; // 先把整个 /api/user/info?username=chen123 拿出来

    // 尝试寻找 URL 中的问号 '?'
    size_t question_mark_pos = raw_url.find('?');
    if (question_mark_pos != std::string::npos)
    {
        // 如果有问号，截取问号前面的作为真实的路由路径
        req.path = raw_url.substr(0, question_mark_pos);

        // 截取问号后面的作为查询参数字符串 (如 "username=chen123&age=20")
        std::string query_string = raw_url.substr(question_mark_pos + 1);

        // 用 '&' 符号进行切割
        std::istringstream query_stream(query_string);
        std::string kv_pair;
        while (std::getline(query_stream, kv_pair, '&'))
        {
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

    size_t headers_end = raw_request.find("\r\n\r\n");
    if (headers_end != std::string::npos)
    {
        std::size_t line_start = first_line_end + 2;
        while (line_start < headers_end)
        {
            std::size_t line_end = raw_request.find("\r\n", line_start);
            if (line_end == std::string::npos || line_end > headers_end)
            {
                break;
            }

            std::string line = raw_request.substr(line_start, line_end - line_start);
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

    // 2. 找到 HTTP 报文中头部和 Body 的分界线（连续的两个回车换行 "\r\n\r\n"）
    size_t body_start = raw_request.find("\r\n\r\n");
    if (body_start != std::string::npos)
    {
        // 提取出纯粹的 Body 数据 (例如 JSON 字符串)
        req.body = raw_request.substr(body_start + 4);
    }

    return req;
}

std::string HttpParser::buildResponse(const HttpResponse &res)
{
    std::string response_str;

    // 1. 拼接状态行 (目前简化处理，200 为 OK，其余统称为 Error)
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
    response_str += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response_str += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    response_str += "Connection: close\r\n";

    // 4. 空行，标志着 Header 的结束和 Body 的开始
    response_str += "\r\n";

    // 5. 拼装具体的业务数据
    response_str += res.body;

    return response_str;
}
