#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <string>

#include "entity/HttpEntity.h"

struct OperatorContext
{
    std::string username;
    int permission_level = 0;
    bool is_admin = false;
};

class AuthService
{
public:
    static bool getOperatorContext(const HttpRequest &req,
                                   OperatorContext &out_context,
                                   std::string &error_message);

    static bool canDeleteOwnedResource(const OperatorContext &context,
                                       const std::string &resource_owner);
};

#endif // AUTH_SERVICE_H
