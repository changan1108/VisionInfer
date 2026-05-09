#ifndef TASK_SERVICE_H
#define TASK_SERVICE_H

#include "entity/TaskEntity.h"

class TaskService
{
public:
    static bool submitTask(TaskEntity &task);
    static bool getTaskById(long long task_id, TaskEntity &out_task);

private:
    static void dispatchAsyncTask(const TaskEntity &task);
};

#endif // TASK_SERVICE_H
