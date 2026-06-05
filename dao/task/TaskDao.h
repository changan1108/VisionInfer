#ifndef TASK_DAO_H
#define TASK_DAO_H

#include <vector>

#include "entity/TaskEntity.h"

class TaskDao
{
public:
    static bool insertTask(TaskEntity &task);
    static bool getTaskById(long long task_id, TaskEntity &out_task);
    static bool updateTaskStatus(long long task_id, const std::string &status);
    static bool markTaskStarted(long long task_id, const std::string &status = TaskStatus::PROCESSING);
    static bool markTaskCompleted(const TaskEntity &task);
    static bool markTaskFailed(long long task_id, const std::string &status, const std::string &error_message);
    static bool markTaskRejected(long long task_id, const std::string &status, const std::string &error_message);
    static bool softDeleteTask(long long task_id, const std::string &deleted_by);
    
    // 更新方法，把任务表中对应任务正式标记为已取消
    static bool markTaskCancelled(long long task_id);
    // Entity 版本
    // ID 版本用于任务尚未深入处理时的简单取消,Entity版本用于处理中取消,保存已经产生的部分处理数据(用于向前端展示已处理的进度，以及用于系统监控的数据)
    static bool markTaskCancelled(const TaskEntity &task);
    // 更新方法，用户刚点击取消，只更新记录“用户请求取消”
    static bool markTaskCancelRequested(long long task_id);
    // 查询用户是否已经请求取消(true-已经请求取消;false-没有请求取消，或者查询失败/记录不存在)
    static bool isTaskCancellationRequested(long long task_id);
    static std::vector<TaskEntity> listTasks(const TaskListFilter &filter);
    static TaskStats getTaskStats();
};

#endif // TASK_DAO_H
