#ifndef MODEL_SERVICE_H
#define MODEL_SERVICE_H

#include <vector>

#include "entity/ModelEntity.h"

class ModelService
{
public:
    static bool addModel(ModelEntity &model, std::string &error_message);
    static bool switchActiveModel(int model_id, std::string &error_message);
    static bool getModelById(int model_id, ModelEntity &out_model);
    static bool getCurrentActiveModel(ModelEntity &out_model);
    static std::vector<ModelEntity> listModels();
    static ModelStats getModelStats();
};

#endif // MODEL_SERVICE_H
