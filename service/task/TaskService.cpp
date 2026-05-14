#include "service/task/TaskService.h"

#include "common/config/AppConfig.h"
#include "common/concurrency/ThreadPool.h"
#include "common/monitor/SystemMonitor.h"
#include "dao/model/ModelDao.h"
#include "dao/task/TaskDao.h"
#include "dao/video/VideoDao.h"
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

bool TaskService::submitTask(TaskEntity &task, std::string &error_message)
{
    if (task.submitted_by.empty() || task.input_video_path.empty() || task.task_type.empty())
    {
        std::cerr << "[TaskService ERROR] 任务提交失败：task_type、submitted_by 或 input_video_path 为空" << std::endl;
        error_message = "task_type、submitted_by 和 input_video_path 为必填项";
        return false;
    }

    if (task.model_id > 0)
    {
        ModelEntity assigned_model;
        if (!ModelDao::getModelById(task.model_id, assigned_model))
        {
            error_message = "指定的 model_id 不存在";
            return false;
        }
    }
    else
    {
        ModelEntity active_model;
        if (ModelDao::getCurrentActiveModel(active_model))
        {
            task.model_id = active_model.id;
        }
    }

    if (task.input_video_id <= 0)
    {
        VideoEntity existing_video;
        if (VideoDao::getVideoByStoredPath(task.input_video_path, existing_video))
        {
            task.input_video_id = existing_video.id;
        }
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
        error_message = "任务入库失败";
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
        error_message = "线程池调度失败";
        return false;
    }

    return true;
}

bool TaskService::getTaskById(long long task_id, TaskEntity &out_task)
{
    return TaskDao::getTaskById(task_id, out_task);
}

std::vector<TaskEntity> TaskService::listTasks(const TaskListFilter &filter)
{
    return TaskDao::listTasks(filter);
}

TaskStats TaskService::getTaskStats()
{
    return TaskDao::getTaskStats();
}

void TaskService::dispatchAsyncTask(const TaskEntity &task)
{
    auto queued_at = std::chrono::steady_clock::now();
    getTaskThreadPool().enqueue([task, queued_at]()
                                {
        auto started_at = std::chrono::steady_clock::now();
        std::uint64_t queue_wait_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(started_at - queued_at).count());
        SystemMonitor::instance().recordTaskQueueWait(queue_wait_ms);
        SystemMonitor::instance().decrementPendingTasks();
        SystemMonitor::instance().incrementActiveThreads();

        try
        {
            TaskDao::markTaskStarted(task.id);

            TaskEntity processed_task;
            bool success = VideoProcessor::processTask(task, processed_task);

            if (success)
            {
                TaskDao::markTaskCompleted(processed_task);
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

        auto finished_at = std::chrono::steady_clock::now();
        std::uint64_t duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count());
        if (duration_ms > 0)
        {
            TaskEntity completed_task;
            if (TaskDao::getTaskById(task.id, completed_task))
            {
                SystemMonitor::instance().recordTaskFrames(completed_task.processed_frame_count, duration_ms);
            }
        }
        SystemMonitor::instance().recordTaskDuration(duration_ms);
        SystemMonitor::instance().decrementActiveThreads(); });
}
