#include "service/model/ModelService.h"

#include "dao/model/ModelDao.h"

bool ModelService::addModel(ModelEntity &model, std::string &error_message)
{
    if (model.model_name.empty() || model.file_path.empty())
    {
        error_message = "model_name 和 file_path 为必填项";
        return false;
    }

    if (model.framework.empty())
    {
        model.framework = "onnx";
    }

    if (model.is_active)
    {
        if (!ModelDao::deactivateAllModels())
        {
            error_message = "取消旧模型激活状态失败";
            return false;
        }
    }

    if (!ModelDao::insertModel(model))
    {
        error_message = "模型入库失败，可能是模型名称重复";
        return false;
    }

    return true;
}

bool ModelService::switchActiveModel(int model_id, std::string &error_message)
{
    ModelEntity existing_model;
    if (!ModelDao::getModelById(model_id, existing_model))
    {
        error_message = "模型不存在";
        return false;
    }

    if (!ModelDao::deactivateAllModels())
    {
        error_message = "取消旧模型激活状态失败";
        return false;
    }

    if (!ModelDao::activateModel(model_id))
    {
        error_message = "激活新模型失败";
        return false;
    }

    return true;
}

bool ModelService::getModelById(int model_id, ModelEntity &out_model)
{
    return ModelDao::getModelById(model_id, out_model);
}

bool ModelService::getCurrentActiveModel(ModelEntity &out_model)
{
    return ModelDao::getCurrentActiveModel(out_model);
}

std::vector<ModelEntity> ModelService::listModels()
{
    return ModelDao::getAllModels();
}

ModelStats ModelService::getModelStats()
{
    return ModelDao::getModelStats();
}
