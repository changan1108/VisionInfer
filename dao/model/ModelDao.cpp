#include "dao/model/ModelDao.h"

#include "common/config/AppConfig.h"
#include "dao/db_conn/MysqlConn.h"
#include "dao/db_conn/MysqlPool.h"

namespace
{
ModelEntity buildModelEntityFromRow(MYSQL_ROW row)
{
    ModelEntity model;
    model.id = row[0] ? std::stoi(row[0]) : 0;
    model.model_name = row[1] ? row[1] : "";
    model.file_path = row[2] ? row[2] : "";
    model.framework = row[3] ? row[3] : "";
    model.is_active = row[4] ? std::stoi(row[4]) != 0 : false;
    model.uploaded_by = row[5] ? row[5] : "";
    model.uploaded_at = row[6] ? row[6] : "";
    model.updated_at = row[7] ? row[7] : "";
    return model;
}
}

bool ModelDao::insertModel(ModelEntity &model)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "INSERT INTO models (model_name, file_path, framework, is_active, uploaded_by) VALUES ('" +
                      model.model_name + "', '" + model.file_path + "', '" + model.framework + "', " +
                      std::to_string(model.is_active ? 1 : 0) + ", '" + model.uploaded_by + "');";

    if (!db->update(sql))
    {
        return false;
    }

    model.id = static_cast<int>(db->getLastInsertId());
    return model.id > 0;
}

bool ModelDao::getModelById(int model_id, ModelEntity &out_model)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT id, model_name, file_path, framework, is_active, uploaded_by, uploaded_at, updated_at FROM models WHERE id = " +
                      std::to_string(model_id) + ";";

    MYSQL_RES *res = db->query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == nullptr)
    {
        mysql_free_result(res);
        return false;
    }

    out_model = buildModelEntityFromRow(row);
    mysql_free_result(res);
    return true;
}

bool ModelDao::getCurrentActiveModel(ModelEntity &out_model)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT id, model_name, file_path, framework, is_active, uploaded_by, uploaded_at, updated_at "
                      "FROM models WHERE is_active = 1 ORDER BY updated_at DESC LIMIT 1;";

    MYSQL_RES *res = db->query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == nullptr)
    {
        mysql_free_result(res);
        return false;
    }

    out_model = buildModelEntityFromRow(row);
    mysql_free_result(res);
    return true;
}

std::vector<ModelEntity> ModelDao::getAllModels()
{
    std::vector<ModelEntity> models;

    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT id, model_name, file_path, framework, is_active, uploaded_by, uploaded_at, updated_at "
                      "FROM models ORDER BY id DESC;";

    MYSQL_RES *res = db->query(sql);
    if (res == nullptr)
    {
        return models;
    }

    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        models.push_back(buildModelEntityFromRow(row));
    }

    mysql_free_result(res);
    return models;
}

bool ModelDao::deactivateAllModels()
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    return db->update("UPDATE models SET is_active = 0;");
}

bool ModelDao::activateModel(int model_id)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE models SET is_active = 1 WHERE id = " + std::to_string(model_id) + ";";
    return db->update(sql);
}

ModelStats ModelDao::getModelStats()
{
    ModelStats stats;

    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    MYSQL_RES *overview_res = db->query("SELECT COUNT(*), SUM(CASE WHEN is_active = 1 THEN 1 ELSE 0 END) FROM models;");
    if (overview_res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(overview_res);
        if (row != nullptr)
        {
            stats.total = row[0] ? std::stoi(row[0]) : 0;
            stats.active_count = row[1] ? std::stoi(row[1]) : 0;
        }
        mysql_free_result(overview_res);
    }

    MYSQL_RES *framework_res = db->query("SELECT framework, COUNT(*) FROM models GROUP BY framework;");
    if (framework_res != nullptr)
    {
        MYSQL_ROW row = nullptr;
        while ((row = mysql_fetch_row(framework_res)) != nullptr)
        {
            std::string framework = row[0] ? row[0] : "";
            int count = row[1] ? std::stoi(row[1]) : 0;
            stats.by_framework[framework] = count;
        }
        mysql_free_result(framework_res);
    }

    ModelEntity active_model;
    if (getCurrentActiveModel(active_model))
    {
        stats.current_active_model = active_model;
        stats.has_active_model = true;
    }

    return stats;
}
