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

    std::string sql = "INSERT INTO tasks (task_name, task_type, submitted_by, input_video_path, output_video_path, video_duration, video_width, video_height, video_fps, frame_interval, confidence_threshold, status, result_summary, error_message, model_id) VALUES ('" +
                      task.task_name + "', '" + task.task_type + "', '" + task.submitted_by + "', '" +
                      task.input_video_path + "', '" + task.output_video_path + "', " +
                      std::to_string(task.video_duration) + ", " +
                      std::to_string(task.video_width) + ", " +
                      std::to_string(task.video_height) + ", " +
                      std::to_string(task.video_fps) + ", " +
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

    std::string sql = "SELECT id, task_name, task_type, submitted_by, input_video_path, output_video_path, video_duration, video_width, video_height, video_fps, frame_interval, confidence_threshold, status, result_summary, error_message, model_id, created_at, started_at, finished_at "
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
    out_task.video_duration = row[6] ? std::stod(row[6]) : 0.0;
    out_task.video_width = row[7] ? std::stoi(row[7]) : 0;
    out_task.video_height = row[8] ? std::stoi(row[8]) : 0;
    out_task.video_fps = row[9] ? std::stod(row[9]) : 0.0;
    out_task.frame_interval = row[10] ? std::stoi(row[10]) : 1;
    out_task.confidence_threshold = row[11] ? std::stod(row[11]) : 0.5;
    out_task.status = row[12] ? row[12] : "";
    out_task.result_summary = row[13] ? row[13] : "";
    out_task.error_message = row[14] ? row[14] : "";
    out_task.model_id = row[15] ? std::stoi(row[15]) : 0;
    out_task.created_at = row[16] ? row[16] : "";
    out_task.started_at = row[17] ? row[17] : "";
    out_task.finished_at = row[18] ? row[18] : "";

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

bool TaskDao::markTaskCompleted(const TaskEntity &task)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "UPDATE tasks SET status = 'COMPLETED', output_video_path = '" + task.output_video_path +
                      "', video_duration = " + std::to_string(task.video_duration) +
                      ", video_width = " + std::to_string(task.video_width) +
                      ", video_height = " + std::to_string(task.video_height) +
                      ", video_fps = " + std::to_string(task.video_fps) +
                      ", result_summary = '" + task.result_summary +
                      "', error_message = '', finished_at = NOW() WHERE id = " + std::to_string(task.id) + ";";
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
