#ifndef _HTTPPARSER_H_
#define _HTTPPARSER_H_
#include <string>
#include "entity/HttpEntity.h"

class HttpParser
{
public:
    // 将前端发来的原始 TCP 字符串（如 "POST /api/user/add HTTP/1.1\r\n..."）解析为 HttpRequest 对象
    static HttpRequest parseRequest(const std::string &raw_request);

    // 将我们业务层处理好的 HttpResponse 对象，打包成标准的 HTTP 协议字符串（准备发给前端）
    static std::string buildResponse(const HttpResponse &res);
};

#endif // _HTTPPARSER_H_