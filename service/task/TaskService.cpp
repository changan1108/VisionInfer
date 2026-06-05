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

/*
状态：因为我们是异步任务，任务提交后就返回task_id，
POST /api/task/submit 返回时，视频还没处理完。前端拿到 task_id 后，会通过任务查询接口查看状态。
所以任务状态就是前端和后台线程之间的沟通桥梁

正常状态流转：PENDING -> QUEUED -> PROCESSING -> COMPLETED
PENDING：任务提交之后，进入线程池之前
QUEUED：已进入第一级线程池阶段(排队调度阶段)
PROCESSING：进入第二级线程池，第二级线程池对其进行处理
COMPLETED：正常从第二级线程池处理完毕

失败和拒绝状态：
拒绝状态
REJECTED_DISK_FULL(注释:拒绝一)
REJECTED_QUEUE_FULL(注释:拒绝二)

失败状态(这些主要来自VideoProcessor阶段)
FAILED_INPUT_NOT_FOUND
FAILED_METADATA
FAILED_INFERENCE
FAILED_ENCODE
FAILED_OUTPUT_COPY
FAILED_RUNTIME

取消状态(任务调度前、视频处理前、视频处理中都会检查是否取消)：
CANCELLED



POST /api/task/submit
  -> TaskService::submitTask
  -> dispatchAsyncTask(task)
  -> 创建 dispatch lambda
  -> 放入 dispatch pool 队列
  -> dispatch 线程取出执行
  -> 状态改为 QUEUED
  -> enqueueVideoProcessingTask(task)
  -> 创建 video process lambda
  -> 放入 video process pool 队列
  -> video process 线程取出执行
  -> 状态改为 PROCESSING
  -> VideoProcessor::processTask
  -> 成功则 COMPLETED，失败则 FAILED

一个提交任务
  -> 一个 新的TaskEntity对象
  -> 一个 新的dispatch lambda函数对象
  -> dispatch pool 中某个已有线程执行它
        (首先在其队列中等待，轮到该lambda时，为其分配一个线程，该线程会：更新其状态为QUEUED，再创建一个视频处理 lambda
        放入视频处理线程池队列)
  -> 一个 新的video process lambda函数对象
  -> video process pool 中某个已有线程执行它
        (首先在其队列中等待，轮到该lambda时，为其分配一个线程，该线程会：更新其状态为PROCESSING，
        并且真正执行视频解码、抽帧和模型推理)
*/

namespace
{

    // 创建并返回任务调度线程池(进入该线程池的任务变为QUEUED(表示已经进入第一阶段(调度阶段))，并投递给视频处理池的队列)
    ThreadPool &getTaskDispatchThreadPool()
    {
        // 创建一个静态对象(线程池对象)
        // 静态对象:第一次调用时创建对象，其生命周期会直到程序结束；后续再调用该函数，会复用该对象而不会新创建
        static ThreadPool pool(AppConfig::TASK_DISPATCH_POOL_SIZE, AppConfig::TASK_DISPATCH_QUEUE_CAPACITY);
        
        // 对象的引用作为返回值类型
        return pool;
    }

    // 创建并返回视频处理线程池(进入该线程池的任务变为PROCESSING，负责 FFmpeg 解码、抽帧、YOLO/ONNX 推理、结果视频生成)
    ThreadPool &getVideoProcessThreadPool()
    {
        // 创建一个静态对象(线程池对象)
        // 静态对象:第一次调用时创建对象，其生命周期会直到程序结束；后续再调用该函数，会复用该对象而不会新创建
        static ThreadPool pool(AppConfig::VIDEO_PROCESS_POOL_SIZE, AppConfig::VIDEO_PROCESS_QUEUE_CAPACITY);
        
        // 对象的引用作为返回值类型
        return pool;
    }

    // 构建默认任务名称
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

// 提交任务
bool TaskService::submitTask(TaskEntity &task, std::string &error_message)
{
    // 检查必要字段
    if (task.submitted_by.empty() || task.input_video_path.empty() || task.task_type.empty())
    {
        std::cerr << "[TaskService ERROR] 任务提交失败：task_type、submitted_by 或 input_video_path 为空" << std::endl;
        error_message = "task_type、submitted_by 和 input_video_path 为必填项";
        return false;
    }

    // 检查使用的模型
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

    // 检查使用的视频
    if (task.input_video_id <= 0)
    {
        VideoEntity existing_video;
        if (VideoDao::getVideoByStoredPath(task.input_video_path, existing_video))
        {
            task.input_video_id = existing_video.id;
        }
    }

    // 检查任务名称+构建任务名称
    if (task.task_name.empty())
    {
        task.task_name = buildDefaultTaskName(task);
    }

    // 磁盘检查
    if (!VideoProcessor::hasSufficientDiskSpaceForTask(task, error_message))
    {
        // 检查若不通过，则为拒绝状态(拒绝一)
        task.status = TaskStatus::REJECTED_DISK_FULL;
        return false;
    }

    // 设置任务初始状态
    task.status = TaskStatus::PENDING;
    task.output_video_path.clear();// 初始化清理
    task.result_summary = "任务已提交，等待线程池调度";
    task.error_message.clear();

    // 任务下达记录写入数据库
    if (!TaskDao::insertTask(task))
    {
        std::cerr << "[TaskService ERROR] 任务入库失败" << std::endl;
        error_message = "任务入库失败";
        return false;
    }

    SystemMonitor::instance().incrementPendingTasks();

    try
    {
        // 进入“线程池线程处理”阶段
        dispatchAsyncTask(task);
    }
    catch (const ThreadPoolQueueFull &)
    {
        SystemMonitor::instance().decrementPendingTasks();
        TaskDao::markTaskRejected(task.id, TaskStatus::REJECTED_QUEUE_FULL, "系统繁忙，任务队列已满，请稍后重试");
        SystemMonitor::instance().incrementRejectedTasks();
        task.status = TaskStatus::REJECTED_QUEUE_FULL;
        error_message = "系统繁忙，任务队列已满，请稍后重试";
        return false;
    }
    catch (const std::exception &ex)
    {
        SystemMonitor::instance().decrementPendingTasks();
        TaskDao::markTaskFailed(task.id, TaskStatus::FAILED_RUNTIME, "线程池调度失败");
        task.status = TaskStatus::FAILED_RUNTIME;
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

TaskExecutionPoolSnapshot TaskService::getExecutionPoolSnapshot()
{
    TaskExecutionPoolSnapshot snapshot;
    snapshot.dispatchLiveThreads = getTaskDispatchThreadPool().size();
    snapshot.dispatchQueueSize = getTaskDispatchThreadPool().queueSize();
    snapshot.dispatchQueueCapacity = getTaskDispatchThreadPool().queueCapacity();
    snapshot.processingLiveThreads = getVideoProcessThreadPool().size();
    snapshot.processingQueueSize = getVideoProcessThreadPool().queueSize();
    snapshot.processingQueueCapacity = getVideoProcessThreadPool().queueCapacity();
    return snapshot;
}

bool TaskService::softDeleteTaskRecord(long long task_id, const std::string &deleted_by)
{
    if (task_id <= 0 || deleted_by.empty())
    {
        return false;
    }

    return TaskDao::softDeleteTask(task_id, deleted_by);
}

bool TaskService::markTaskCancelled(long long task_id)
{
    if (task_id <= 0)
    {
        return false;
    }

    return TaskDao::markTaskCancelled(task_id);
}

bool TaskService::requestTaskCancellation(long long task_id)
{
    if (task_id <= 0)
    {
        return false;
    }

    return TaskDao::markTaskCancelRequested(task_id);
}

// 使用第一级线程池调度任务
void TaskService::dispatchAsyncTask(const TaskEntity &task)
{
    auto queued_at = std::chrono::steady_clock::now();

    // 调用返回任务调度线程池的实例，之后利用该线程池的方法，将任务参数对象+lambda函数对象入队(ThreadPool类维护一个队列)
    // 入第一级线程池的队列，入队后会等待一个线程执行该lambda函数对象；其中lambda规定了本次任务的工作内容
    getTaskDispatchThreadPool().enqueue([task, queued_at]()
                                        {
        // 当前lambda规定的任务内容:
        // "任务取消"的检查点
        if (TaskDao::isTaskCancellationRequested(task.id))
        {
            SystemMonitor::instance().decrementPendingTasks();
            TaskDao::markTaskCancelled(task.id);
            return;
        }

        // 更新任务状态为QUEUED(已进入调度阶段)
        TaskDao::updateTaskStatus(task.id, TaskStatus::QUEUED);

        try
        {
            // 将任务参数实体 投递到第二级线程池
            TaskService::enqueueVideoProcessingTask(task, queued_at);
        }
        // 处理线程池的队列已满
        catch (const ThreadPoolQueueFull &)
        {
            SystemMonitor::instance().decrementPendingTasks();
            // 设置拒绝状态(拒绝二)
            TaskDao::markTaskRejected(task.id, TaskStatus::REJECTED_QUEUE_FULL, "系统繁忙，视频处理队列已满，请稍后重试");
            SystemMonitor::instance().incrementRejectedTasks();
        }
        catch (const std::exception &ex)
        {
            SystemMonitor::instance().decrementPendingTasks();
            TaskDao::markTaskFailed(task.id, TaskStatus::FAILED_RUNTIME, ex.what());
            SystemMonitor::instance().incrementFailedTasks();
        }
        catch (...)
        {
            SystemMonitor::instance().decrementPendingTasks();
            TaskDao::markTaskFailed(task.id, TaskStatus::FAILED_RUNTIME, "视频处理调度异常");
            SystemMonitor::instance().incrementFailedTasks();
        } });
}

// 使用第二级线程池处理任务
void TaskService::enqueueVideoProcessingTask(const TaskEntity &task,
                                             std::chrono::steady_clock::time_point queued_at)
{
    // 调用方法，返回第二级线程池实例，调用该实例的入队方法，将
    // 将第一级线程池传入的任务参数对象+生成新的lambda函数对象，入队，放入第二级线程池的待处理队列，等待一个第二级线程池的线程执行该lambda
    getVideoProcessThreadPool().enqueue([task, queued_at]()
                                        {
        // lambda的内容:
        auto started_at = std::chrono::steady_clock::now();
        std::uint64_t queue_wait_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(started_at - queued_at).count());
        SystemMonitor::instance().recordTaskQueueWait(queue_wait_ms);
        SystemMonitor::instance().decrementPendingTasks();
        SystemMonitor::instance().incrementActiveThreads();

        try
        {
            // "任务取消"的检查点
            if (TaskDao::isTaskCancellationRequested(task.id))
            {
                TaskDao::markTaskCancelled(task.id);
            }
            else
            {
                // 更新状态为PROCESSING
                TaskDao::markTaskStarted(task.id, TaskStatus::PROCESSING);

                TaskEntity processed_task;
                // 执行视频处理
                bool success = VideoProcessor::processTask(task,
                                                           processed_task,
                                                           [task]()
                                                           {
                                                               return TaskDao::isTaskCancellationRequested(task.id);
                                                           });
                // 成功后，数据库对应记录更新任务状态
                if (success && !TaskDao::isTaskCancellationRequested(task.id))
                {
                    // 更新状态字段，以及其他结果字段
                    TaskDao::markTaskCompleted(processed_task);
                    SystemMonitor::instance().incrementCompletedTasks();
                }
                else if (processed_task.status == TaskStatus::CANCELLED ||
                         TaskDao::isTaskCancellationRequested(task.id))
                {
                    if (processed_task.id <= 0)
                    {
                        processed_task = task;
                    }
                    processed_task.id = task.id;
                    if (processed_task.result_summary.empty())
                    {
                        processed_task.result_summary = "任务已取消，处理线程已安全退出";
                    }
                    TaskDao::markTaskCancelled(processed_task);
                }
                else
                {
                    TaskDao::markTaskFailed(task.id,
                                            VideoProcessor::inferFailureStatus(processed_task),
                                            processed_task.error_message.empty() ? "视频处理失败" : processed_task.error_message);
                    SystemMonitor::instance().incrementFailedTasks();
                }
            }
        }
        catch (const std::exception &ex)
        {
            TaskDao::markTaskFailed(task.id, TaskStatus::FAILED_RUNTIME, ex.what());
            SystemMonitor::instance().incrementFailedTasks();
        }
        catch (...)
        {
            TaskDao::markTaskFailed(task.id, TaskStatus::FAILED_RUNTIME, "未知异常");
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
