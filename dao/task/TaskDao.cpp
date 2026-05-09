#include "dao/task/TaskDao.h"

#include "common/config/AppConfig.h"
#include "dao/db_conn/MysqlConn.h"

#include <iostream>

bool TaskDao::insertTask(TaskEntity &task)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string model_id_value = task.model_id > 0 ? std::to_string(task.model_id) : "NULL";

    std::string sql = "INSERT INTO tasks (task_name, task_type, submitted_by, input_video_path, output_video_path, frame_interval, confidence_threshold, status, result_summary, error_message, model_id) VALUES ('" +
                      task.task_name + "', '" + task.task_type + "', '" + task.submitted_by + "', '" +
                      task.input_video_path + "', '" + task.output_video_path + "', " +
                      std::to_string(task.frame_interval) + ", " + std::to_string(task.confidence_threshold) + ", '" +
                      task.status + "', '" + task.result_summary + "', '" +
                      task.error_message + "', " + model_id_value + ");";

    if (!db.update(sql))
    {
        return false;
    }

    task.id = db.getLastInsertId();
    return task.id > 0;
}

bool TaskDao::getTaskById(long long task_id, TaskEntity &out_task)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "SELECT id, task_name, task_type, submitted_by, input_video_path, output_video_path, frame_interval, confidence_threshold, status, result_summary, error_message, model_id, created_at, started_at, finished_at "
                      "FROM tasks WHERE id = " +
                      std::to_string(task_id) + ";";

    MYSQL_RES *res = db.query(sql);
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

    out_task.id = row[0] ? std::stoll(row[0]) : 0;
    out_task.task_name = row[1] ? row[1] : "";
    out_task.task_type = row[2] ? row[2] : "";
    out_task.submitted_by = row[3] ? row[3] : "";
    out_task.input_video_path = row[4] ? row[4] : "";
    out_task.output_video_path = row[5] ? row[5] : "";
    out_task.frame_interval = row[6] ? std::stoi(row[6]) : 1;
    out_task.confidence_threshold = row[7] ? std::stod(row[7]) : 0.5;
    out_task.status = row[8] ? row[8] : "";
    out_task.result_summary = row[9] ? row[9] : "";
    out_task.error_message = row[10] ? row[10] : "";
    out_task.model_id = row[11] ? std::stoi(row[11]) : 0;
    out_task.created_at = row[12] ? row[12] : "";
    out_task.started_at = row[13] ? row[13] : "";
    out_task.finished_at = row[14] ? row[14] : "";

    mysql_free_result(res);
    return true;
}

bool TaskDao::updateTaskStatus(long long task_id, const std::string &status)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "UPDATE tasks SET status = '" + status + "' WHERE id = " + std::to_string(task_id) + ";";
    return db.update(sql);
}

bool TaskDao::markTaskStarted(long long task_id)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "UPDATE tasks SET status = 'PROCESSING', started_at = NOW() WHERE id = " + std::to_string(task_id) + ";";
    return db.update(sql);
}

bool TaskDao::markTaskCompleted(long long task_id, const std::string &output_video_path,
                                const std::string &result_summary)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "UPDATE tasks SET status = 'COMPLETED', output_video_path = '" + output_video_path +
                      "', result_summary = '" + result_summary +
                      "', error_message = '', finished_at = NOW() WHERE id = " + std::to_string(task_id) + ";";
    return db.update(sql);
}

bool TaskDao::markTaskFailed(long long task_id, const std::string &error_message)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "UPDATE tasks SET status = 'FAILED', error_message = '" + error_message +
                      "', finished_at = NOW() WHERE id = " + std::to_string(task_id) + ";";
    return db.update(sql);
}
