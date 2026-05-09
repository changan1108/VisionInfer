#include "service/video/VideoProcessor.h"

#include "common/config/AppConfig.h"

#include <chrono>
#include <thread>

namespace
{
std::string buildOutputPath(const std::string &input_path, long long task_id)
{
    std::string file_name = input_path;
    std::size_t slash_pos = file_name.find_last_of("/\\");
    if (slash_pos != std::string::npos)
    {
        file_name = file_name.substr(slash_pos + 1);
    }

    std::size_t dot_pos = file_name.find_last_of('.');
    if (dot_pos != std::string::npos)
    {
        file_name = file_name.substr(0, dot_pos);
    }

    return std::string(AppConfig::VIDEO_OUTPUT_DIR) + "/" + file_name + "_task_" + std::to_string(task_id) + "_result.mp4";
}
}

bool VideoProcessor::processTask(const TaskEntity &task, TaskEntity &out_result)
{
    // 当前阶段先用模拟处理替代真实 FFmpeg/YOLO 推理，先把异步任务主链路跑通。
    std::this_thread::sleep_for(std::chrono::seconds(2));

    out_result = task;
    out_result.output_video_path = buildOutputPath(task.input_video_path, task.id);
    out_result.result_summary = "模拟分析完成，类型=" + task.task_type +
                                "，抽帧间隔=" + std::to_string(task.frame_interval) +
                                "，置信度阈值=" + std::to_string(task.confidence_threshold);
    out_result.status = "COMPLETED";
    out_result.error_message.clear();
    return true;
}
