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
    static bool markTaskStarted(long long task_id);
    static bool markTaskCompleted(const TaskEntity &task);
    static bool markTaskFailed(long long task_id, const std::string &error_message);
    static std::vector<TaskEntity> listTasks(const TaskListFilter &filter);
    static TaskStats getTaskStats();
};

#endif // TASK_DAO_H
