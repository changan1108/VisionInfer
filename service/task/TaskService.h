#ifndef TASK_SERVICE_H
#define TASK_SERVICE_H

#include <chrono>
#include <vector>

#include "entity/TaskEntity.h"

struct TaskExecutionPoolSnapshot
{
    std::size_t dispatchLiveThreads = 0;
    std::size_t dispatchQueueSize = 0;
    std::size_t dispatchQueueCapacity = 0;
    std::size_t processingLiveThreads = 0;
    std::size_t processingQueueSize = 0;
    std::size_t processingQueueCapacity = 0;
};

class TaskService
{
public:
    // 提交任务
    static bool submitTask(TaskEntity &task, std::string &error_message);

    static bool getTaskById(long long task_id, TaskEntity &out_task);
    static std::vector<TaskEntity> listTasks(const TaskListFilter &filter);
    static TaskStats getTaskStats();
    static TaskExecutionPoolSnapshot getExecutionPoolSnapshot();
    static bool softDeleteTaskRecord(long long task_id, const std::string &deleted_by);
    static bool markTaskCancelled(long long task_id);
    static bool requestTaskCancellation(long long task_id);

private:

    // 使用第一级线程池调度任务
    static void dispatchAsyncTask(const TaskEntity &task);
    // 使用第二级线程池处理任务
    static void enqueueVideoProcessingTask(const TaskEntity &task,
                                           std::chrono::steady_clock::time_point queued_at);
};

#endif // TASK_SERVICE_H
