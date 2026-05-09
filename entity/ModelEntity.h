#ifndef MODEL_ENTITY_H
#define MODEL_ENTITY_H

#include <string>

struct ModelEntity
{
    int id = 0;
    std::string model_name;
    std::string file_path;
    std::string framework;
    bool is_active = false;
    std::string uploaded_by;
    std::string uploaded_at;
    std::string updated_at;
};

#endif // MODEL_ENTITY_H
