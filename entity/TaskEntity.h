#ifndef TASK_ENTITY_H
#define TASK_ENTITY_H

#include <string>

struct TaskEntity
{
    long long id = 0;
    std::string task_name;
    std::string task_type;
    std::string submitted_by;
    std::string input_video_path;
    std::string output_video_path;
    int frame_interval = 1;
    double confidence_threshold = 0.5;
    std::string status;
    std::string result_summary;
    std::string error_message;
    int model_id = 0;
    std::string created_at;
    std::string started_at;
    std::string finished_at;
};

#endif // TASK_ENTITY_H
