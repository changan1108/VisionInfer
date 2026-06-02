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
    static bool markTaskCancelled(long long task_id);
    static bool markTaskCancelled(const TaskEntity &task);
    static bool markTaskCancelRequested(long long task_id);
    static bool isTaskCancellationRequested(long long task_id);
    static std::vector<TaskEntity> listTasks(const TaskListFilter &filter);
    static TaskStats getTaskStats();
};

#endif // TASK_DAO_H
