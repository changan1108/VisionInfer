#ifndef TASK_SERVICE_H
#define TASK_SERVICE_H

#include <vector>

#include "entity/TaskEntity.h"

class TaskService
{
public:
    static bool submitTask(TaskEntity &task, std::string &error_message);
    static bool getTaskById(long long task_id, TaskEntity &out_task);
    static std::vector<TaskEntity> listTasks(const TaskListFilter &filter);
    static TaskStats getTaskStats();

private:
    static void dispatchAsyncTask(const TaskEntity &task);
};

#endif // TASK_SERVICE_H
