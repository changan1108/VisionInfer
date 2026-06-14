#ifndef _HTTP_ENTITY_H_
#define _HTTP_ENTITY_H_

#include <string>
#include <unordered_map>

// HTTP 请求实体
struct HttpRequest
{
    std::string method;                                       // 例如 "GET" 或 "POST"
    std::string path;                                         // 例如 "/api/user/info"
    std::string body;                                         // 前端发来的请求体内容： JSON 字符串数据、表单或文件二进制内容(用于POST请求)
    std::unordered_map<std::string, std::string> headers;     // 请求头
    std::unordered_map<std::string, std::string> queryParams; // 用来存放 URL ? 后面的键值对参数(用于GET请求)
};

// HTTP 响应实体
struct HttpResponse
{
    int statusCode = 200;                                  // 默认状态码 200 代表成功
    std::string body;                                      // 返回给前端的响应体
    std::string contentType = "application/json; charset=utf-8";
    std::string contentDisposition;
};

#endif //_HTTP_ENTITY_H_
