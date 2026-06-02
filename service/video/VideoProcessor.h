#ifndef VIDEO_PROCESSOR_H
#define VIDEO_PROCESSOR_H

#include "entity/TaskEntity.h"

#include <functional>

class VideoProcessor
{
public:
    static bool processTask(const TaskEntity &task, TaskEntity &out_result);
    static bool processTask(const TaskEntity &task,
                            TaskEntity &out_result,
                            const std::function<bool()> &is_cancel_requested);
    static bool hasSufficientDiskSpaceForTask(const TaskEntity &task, std::string &error_message);
    static std::string inferFailureStatus(const TaskEntity &task_result);
};

#endif // VIDEO_PROCESSOR_H
