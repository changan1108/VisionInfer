#include "dao/task/TaskDao.h"

#include "common/config/AppConfig.h"
#include "dao/db_conn/MysqlConn.h"
#include "dao/db_conn/MysqlPool.h"

#include <iostream>

namespace
{
std::string escapeSql(const std::string &input)
{
    std::string escaped;
    escaped.reserve(input.size() * 2);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' || input[i] == '\'')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(input[i]);
    }
    return escaped;
}

TaskEntity buildTaskEntityFromRow(MYSQL_ROW row)
{
    TaskEntity task;
    task.id = row[0] ? std::stoll(row[0]) : 0;
    task.task_name = row[1] ? row[1] : "";
    task.task_type = row[2] ? row[2] : "";
    task.submitted_by = row[3] ? row[3] : "";
    task.input_video_path = row[4] ? row[4] : "";
    task.input_video_id = row[5] ? std::stoi(row[5]) : 0;
    task.output_video_path = row[6] ? row[6] : "";
    task.video_duration = row[7] ? std::stod(row[7]) : 0.0;
    task.video_width = row[8] ? std::stoi(row[8]) : 0;
    task.video_height = row[9] ? std::stoi(row[9]) : 0;
    task.video_fps = row[10] ? std::stod(row[10]) : 0.0;
    task.frame_interval = row[11] ? std::stoi(row[11]) : 1;
    task.confidence_threshold = row[12] ? std::stod(row[12]) : 0.5;
    task.processed_frame_count = row[13] ? std::stoi(row[13]) : 0;
    task.detection_count = row[14] ? std::stoi(row[14]) : 0;
    task.real_inference_executed = row[15] ? std::stoi(row[15]) != 0 : false;
    task.result_video_generated = row[16] ? std::stoi(row[16]) != 0 : false;
    task.used_model_name = row[17] ? row[17] : "";
    task.used_model_framework = row[18] ? row[18] : "";
    task.video_build_mode = row[19] ? row[19] : "";
    task.inference_runtime_message = row[20] ? row[20] : "";
    task.status = row[21] ? row[21] : "";
    task.result_summary = row[22] ? row[22] : "";
    task.error_message = row[23] ? row[23] : "";
    task.model_id = row[24] ? std::stoi(row[24]) : 0;
    task.created_at = row[25] ? row[25] : "";
    task.started_at = row[26] ? row[26] : "";
    task.finished_at = row[27] ? row[27] : "";
    return task;
}
}

bool TaskDao::insertTask(TaskEntity &task)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string model_id_value = task.model_id > 0 ? std::to_string(task.model_id) : "NULL";
    std::string input_video_id_value = task.input_video_id > 0 ? std::to_string(task.input_video_id) : "NULL";

    std::string sql = "INSERT INTO tasks (task_name, task_type, submitted_by, input_video_path, input_video_id, output_video_path, video_duration, video_width, video_height, video_fps, frame_interval, confidence_threshold, processed_frame_count, detection_count, real_inference_executed, result_video_generated, used_model_name, used_model_framework, video_build_mode, inference_runtime_message, status, result_summary, error_message, model_id) VALUES ('" +
                      escapeSql(task.task_name) + "', '" + escapeSql(task.task_type) + "', '" + escapeSql(task.submitted_by) + "', '" +
                      escapeSql(task.input_video_path) + "', " + input_video_id_value + ", '" + escapeSql(task.output_video_path) + "', " +
                      std::to_string(task.video_duration) + ", " +
                      std::to_string(task.video_width) + ", " +
                      std::to_string(task.video_height) + ", " +
                      std::to_string(task.video_fps) + ", " +
                      std::to_string(task.frame_interval) + ", " + std::to_string(task.confidence_threshold) + ", " +
                      std::to_string(task.processed_frame_count) + ", " +
                      std::to_string(task.detection_count) + ", " +
                      std::to_string(task.real_inference_executed ? 1 : 0) + ", " +
                      std::to_string(task.result_video_generated ? 1 : 0) + ", '" +
                      escapeSql(task.used_model_name) + "', '" +
                      escapeSql(task.used_model_framework) + "', '" +
                      escapeSql(task.video_build_mode) + "', '" +
                      escapeSql(task.inference_runtime_message) + "', '" +
                      escapeSql(task.status) + "', '" + escapeSql(task.result_summary) + "', '" +
                      escapeSql(task.error_message) + "', " + model_id_value + ");";

    if (!db->update(sql))
    {
        return false;
    }

    task.id = db->getLastInsertId();
    return task.id > 0;
}

bool TaskDao::getTaskById(long long task_id, TaskEntity &out_task)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT id, task_name, task_type, submitted_by, input_video_path, input_video_id, output_video_path, video_duration, video_width, video_height, video_fps, frame_interval, confidence_threshold, processed_frame_count, detection_count, real_inference_executed, result_video_generated, used_model_name, used_model_framework, video_build_mode, inference_runtime_message, status, result_summary, error_message, model_id, created_at, started_at, finished_at "
                      "FROM tasks WHERE id = " +
                      std::to_string(task_id) + ";";

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

    out_task = buildTaskEntityFromRow(row);

    mysql_free_result(res);
    return true;
}

bool TaskDao::updateTaskStatus(long long task_id, const std::string &status)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE tasks SET status = '" + escapeSql(status) + "' WHERE id = " + std::to_string(task_id) + ";";
    return db->update(sql);
}

bool TaskDao::markTaskStarted(long long task_id, const std::string &status)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE tasks SET status = '" + escapeSql(status) + "', started_at = NOW() WHERE id = " + std::to_string(task_id) + ";";
    return db->update(sql);
}

bool TaskDao::markTaskCompleted(const TaskEntity &task)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE tasks SET status = 'COMPLETED', output_video_path = '" + escapeSql(task.output_video_path) +
                      "', video_duration = " + std::to_string(task.video_duration) +
                      ", video_width = " + std::to_string(task.video_width) +
                      ", video_height = " + std::to_string(task.video_height) +
                      ", video_fps = " + std::to_string(task.video_fps) +
                      ", processed_frame_count = " + std::to_string(task.processed_frame_count) +
                      ", detection_count = " + std::to_string(task.detection_count) +
                      ", real_inference_executed = " + std::to_string(task.real_inference_executed ? 1 : 0) +
                      ", result_video_generated = " + std::to_string(task.result_video_generated ? 1 : 0) +
                      ", used_model_name = '" + escapeSql(task.used_model_name) +
                      "', used_model_framework = '" + escapeSql(task.used_model_framework) +
                      "', video_build_mode = '" + escapeSql(task.video_build_mode) +
                      "', inference_runtime_message = '" + escapeSql(task.inference_runtime_message) +
                      "', result_summary = '" + escapeSql(task.result_summary) +
                      "', error_message = '', finished_at = NOW() WHERE id = " + std::to_string(task.id) + ";";
    return db->update(sql);
}

bool TaskDao::markTaskFailed(long long task_id, const std::string &status, const std::string &error_message)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE tasks SET status = '" + escapeSql(status) + "', error_message = '" + escapeSql(error_message) +
                      "', finished_at = NOW() WHERE id = " + std::to_string(task_id) + ";";
    return db->update(sql);
}

bool TaskDao::markTaskRejected(long long task_id, const std::string &status, const std::string &error_message)
{
    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "UPDATE tasks SET status = '" + escapeSql(status) + "', error_message = '" + escapeSql(error_message) +
                      "', finished_at = NOW() WHERE id = " + std::to_string(task_id) + ";";
    return db->update(sql);
}

std::vector<TaskEntity> TaskDao::listTasks(const TaskListFilter &filter)
{
    std::vector<TaskEntity> tasks;
    int limit = filter.limit;
    if (limit <= 0)
    {
        limit = 10;
    }

    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    std::string sql = "SELECT id, task_name, task_type, submitted_by, input_video_path, input_video_id, output_video_path, video_duration, video_width, video_height, video_fps, frame_interval, confidence_threshold, processed_frame_count, detection_count, real_inference_executed, result_video_generated, used_model_name, used_model_framework, video_build_mode, inference_runtime_message, status, result_summary, error_message, model_id, created_at, started_at, finished_at "
                      "FROM tasks WHERE 1=1";

    if (!filter.status.empty())
    {
        sql += " AND status = '" + escapeSql(filter.status) + "'";
    }
    if (!filter.task_type.empty())
    {
        sql += " AND task_type = '" + escapeSql(filter.task_type) + "'";
    }
    if (!filter.submitted_by.empty())
    {
        sql += " AND submitted_by = '" + escapeSql(filter.submitted_by) + "'";
    }

    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit) + ";";

    MYSQL_RES *res = db->query(sql);
    if (res == nullptr)
    {
        return tasks;
    }

    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        tasks.push_back(buildTaskEntityFromRow(row));
    }

    mysql_free_result(res);
    return tasks;
}

TaskStats TaskDao::getTaskStats()
{
    TaskStats stats;

    MysqlPool::BorrowedConn db = MysqlPool::instance().acquire();

    MYSQL_RES *overview_res = db->query("SELECT COUNT(*), "
                                        "SUM(CASE WHEN result_video_generated = 1 THEN 1 ELSE 0 END), "
                                        "SUM(CASE WHEN real_inference_executed = 1 THEN 1 ELSE 0 END) "
                                        "FROM tasks;");
    if (overview_res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(overview_res);
        if (row != nullptr)
        {
            stats.total = row[0] ? std::stoi(row[0]) : 0;
            stats.result_video_generated = row[1] ? std::stoi(row[1]) : 0;
            stats.real_inference_executed = row[2] ? std::stoi(row[2]) : 0;
        }
        mysql_free_result(overview_res);
    }

    MYSQL_RES *status_res = db->query("SELECT status, COUNT(*) FROM tasks GROUP BY status;");
    if (status_res != nullptr)
    {
        MYSQL_ROW row = nullptr;
        while ((row = mysql_fetch_row(status_res)) != nullptr)
        {
            std::string status = row[0] ? row[0] : "";
            int count = row[1] ? std::stoi(row[1]) : 0;
            stats.by_status[status] = count;
        }
        mysql_free_result(status_res);
    }

    MYSQL_RES *type_res = db->query("SELECT task_type, COUNT(*) FROM tasks GROUP BY task_type;");
    if (type_res != nullptr)
    {
        MYSQL_ROW row = nullptr;
        while ((row = mysql_fetch_row(type_res)) != nullptr)
        {
            std::string task_type = row[0] ? row[0] : "";
            int count = row[1] ? std::stoi(row[1]) : 0;
            stats.by_task_type[task_type] = count;
        }
        mysql_free_result(type_res);
    }

    return stats;
}
