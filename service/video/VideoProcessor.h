#ifndef VIDEO_PROCESSOR_H
#define VIDEO_PROCESSOR_H

#include "entity/TaskEntity.h"

#include <functional>

class VideoProcessor
{
public:
    // 简化重载版，内部测试使用，项目最终实际不使用
    static bool processTask(const TaskEntity &task, TaskEntity &out_result);

    // 第二级线程池线程执行的lambda内的：“任务真正的视频处理函数”(参数一:任务参数实体；参数二:输出处理结果参数；参数三:取消检查函数)
    static bool processTask(const TaskEntity &task,
                            TaskEntity &out_result,
                            const std::function<bool()> &is_cancel_requested);

    
    static bool hasSufficientDiskSpaceForTask(const TaskEntity &task, std::string &error_message);
    static std::string inferFailureStatus(const TaskEntity &task_result);
};

#endif // VIDEO_PROCESSOR_H
