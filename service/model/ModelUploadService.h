#ifndef MODEL_UPLOAD_SERVICE_H
#define MODEL_UPLOAD_SERVICE_H

#include <string>

#include "entity/ModelEntity.h"

struct ModelUploadRequest
{
    std::string model_name;
    std::string framework;
    std::string uploaded_by;
    bool is_active = false;
    std::string original_filename;
    std::string file_content;
};

struct ModelUploadResult
{
    ModelEntity model;
    std::string stored_filename;
};

class ModelUploadService
{
public:
    static bool uploadModel(const ModelUploadRequest &request, ModelUploadResult &result,
                            std::string &error_message);
};

#endif // MODEL_UPLOAD_SERVICE_H
