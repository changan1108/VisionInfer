#ifndef MODEL_ENTITY_H
#define MODEL_ENTITY_H

#include <map>
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

struct ModelStats
{
    int total = 0;
    int active_count = 0;
    std::map<std::string, int> by_framework;
    ModelEntity current_active_model;
    bool has_active_model = false;
};

#endif // MODEL_ENTITY_H
