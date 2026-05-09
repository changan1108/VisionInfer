#ifndef VIDEO_PROCESSOR_H
#define VIDEO_PROCESSOR_H

#include "entity/TaskEntity.h"

class VideoProcessor
{
public:
    static bool processTask(const TaskEntity &task, TaskEntity &out_result);
};

#endif // VIDEO_PROCESSOR_H
