#ifndef VIDEO_PROCESSOR_H
#define VIDEO_PROCESSOR_H

#include "entity/TaskEntity.h"

class VideoProcessor
{
public:
    static bool processTask(const TaskEntity &task, TaskEntity &out_result);
    static bool hasSufficientDiskSpaceForTask(const TaskEntity &task, std::string &error_message);
    static std::string inferFailureStatus(const TaskEntity &task_result);
};

#endif // VIDEO_PROCESSOR_H
