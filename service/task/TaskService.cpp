#include "service/task/TaskService.h"

#include "common/config/AppConfig.h"
#include "common/concurrency/ThreadPool.h"
#include "common/monitor/SystemMonitor.h"
#include "dao/task/TaskDao.h"
#include "service/video/VideoProcessor.h"

#include <exception>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace
{
ThreadPool &getTaskThreadPool()
{
    static ThreadPool pool(AppConfig::DEFAULT_THREAD_POOL_SIZE);
    return pool;
}

std::string buildDefaultTaskName(const TaskEntity &task)
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&now_c);

    std::ostringstream oss;
    oss << task.task_type << "_" << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
    return oss.str();
}
}

bool TaskService::submitTask(TaskEntity &task)
{
    if (task.submitted_by.empty() || task.input_video_path.empty() || task.task_type.empty())
    {
        std::cerr << "[TaskService ERROR] 任务提交失败：task_type、submitted_by 或 input_video_path 为空" << std::endl;
        return false;
    }

    if (task.task_name.empty())
    {
        task.task_name = buildDefaultTaskName(task);
    }

    task.status = "PENDING";
    task.output_video_path.clear();
    task.result_summary = "任务已提交，等待线程池调度";
    task.error_message.clear();

    if (!TaskDao::insertTask(task))
    {
        std::cerr << "[TaskService ERROR] 任务入库失败" << std::endl;
        return false;
    }

    SystemMonitor::instance().incrementPendingTasks();

    try
    {
        dispatchAsyncTask(task);
    }
    catch (const std::exception &ex)
    {
        SystemMonitor::instance().decrementPendingTasks();
        TaskDao::markTaskFailed(task.id, "线程池调度失败");
        std::cerr << "[TaskService ERROR] 线程池调度失败: " << ex.what() << std::endl;
        return false;
    }

    return true;
}

bool TaskService::getTaskById(long long task_id, TaskEntity &out_task)
{
    return TaskDao::getTaskById(task_id, out_task);
}

void TaskService::dispatchAsyncTask(const TaskEntity &task)
{
    getTaskThreadPool().enqueue([task]()
                                {
        SystemMonitor::instance().decrementPendingTasks();
        SystemMonitor::instance().incrementActiveThreads();

        try
        {
            TaskDao::markTaskStarted(task.id);

            TaskEntity processed_task;
            bool success = VideoProcessor::processTask(task, processed_task);

            if (success)
            {
                TaskDao::markTaskCompleted(task.id, processed_task.output_video_path, processed_task.result_summary);
                SystemMonitor::instance().incrementCompletedTasks();
            }
            else
            {
                TaskDao::markTaskFailed(task.id, "视频处理失败");
                SystemMonitor::instance().incrementFailedTasks();
            }
        }
        catch (const std::exception &ex)
        {
            TaskDao::markTaskFailed(task.id, ex.what());
            SystemMonitor::instance().incrementFailedTasks();
        }
        catch (...)
        {
            TaskDao::markTaskFailed(task.id, "未知异常");
            SystemMonitor::instance().incrementFailedTasks();
        }

        SystemMonitor::instance().decrementActiveThreads(); });
}
