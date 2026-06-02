#ifndef TASK_ENTITY_H
#define TASK_ENTITY_H

#include <map>
#include <string>

namespace TaskStatus
{
constexpr const char *PENDING = "PENDING";
constexpr const char *QUEUED = "QUEUED";
constexpr const char *PROCESSING = "PROCESSING";
constexpr const char *COMPLETED = "COMPLETED";
constexpr const char *REJECTED_QUEUE_FULL = "REJECTED_QUEUE_FULL";
constexpr const char *REJECTED_DISK_FULL = "REJECTED_DISK_FULL";
constexpr const char *FAILED_INPUT_NOT_FOUND = "FAILED_INPUT_NOT_FOUND";
constexpr const char *FAILED_OUTPUT_DIR = "FAILED_OUTPUT_DIR";
constexpr const char *FAILED_METADATA = "FAILED_METADATA";
constexpr const char *FAILED_INFERENCE = "FAILED_INFERENCE";
constexpr const char *FAILED_ENCODE = "FAILED_ENCODE";
constexpr const char *FAILED_OUTPUT_COPY = "FAILED_OUTPUT_COPY";
constexpr const char *FAILED_RUNTIME = "FAILED_RUNTIME";
constexpr const char *CANCELLED = "CANCELLED";
}

struct TaskEntity
{
    long long id = 0;
    std::string task_name;
    std::string task_type;
    std::string submitted_by;
    std::string input_video_path;
    int input_video_id = 0;
    std::string output_video_path;
    double video_duration = 0.0;
    int video_width = 0;
    int video_height = 0;
    double video_fps = 0.0;
    int frame_interval = 1;
    double confidence_threshold = 0.5;
    int processed_frame_count = 0;
    int detection_count = 0;
    bool real_inference_executed = false;
    bool result_video_generated = false;
    std::string used_model_name;
    std::string used_model_framework;
    std::string video_build_mode;
    std::string inference_runtime_message;
    std::string status;
    std::string result_summary;
    std::string error_message;
    int model_id = 0;
    std::string created_at;
    std::string started_at;
    std::string finished_at;
};

struct TaskListFilter
{
    int limit = 10;
    std::string status;
    std::string task_type;
    std::string submitted_by;
};

struct TaskStats
{
    int total = 0;
    int result_video_generated = 0;
    int real_inference_executed = 0;
    std::map<std::string, int> by_status;
    std::map<std::string, int> by_task_type;
};

#endif // TASK_ENTITY_H
