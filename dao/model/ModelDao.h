#ifndef MODEL_DAO_H
#define MODEL_DAO_H

#include <vector>

#include "entity/ModelEntity.h"

class ModelDao
{
public:
    static bool insertModel(ModelEntity &model);
    static bool getModelById(int model_id, ModelEntity &out_model);
    static bool getCurrentActiveModel(ModelEntity &out_model);
    static std::vector<ModelEntity> getAllModels();
    static bool deactivateAllModels();
    static bool activateModel(int model_id);
    static bool softDeleteModel(int model_id, const std::string &deleted_by);
    static ModelStats getModelStats();
};

#endif // MODEL_DAO_H
