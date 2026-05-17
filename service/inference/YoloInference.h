#ifndef YOLO_INFERENCE_H
#define YOLO_INFERENCE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct DetectionBox
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    std::string label;
    float confidence = 0.0f;
};

struct FrameBuffer
{
    int width = 0;
    int height = 0;
    int linesize = 0;
    unsigned char *data = nullptr;
};

struct InferenceResult
{
    int detection_count = 0;
    std::vector<DetectionBox> boxes;
    std::string model_name;
    std::string model_framework;
    bool real_inference_ran = false;
    int output_tensor_count = 0;
    std::string runtime_message;
    std::string summary;
    std::uint64_t forward_duration_ms = 0;
    std::uint64_t postprocess_duration_ms = 0;
    std::uint64_t draw_duration_ms = 0;
};

struct InferenceModelContext
{
    int model_id = 0;
    std::string model_name;
    std::string model_path;
    std::string framework;
    bool file_exists = false;
    bool uses_active_model = false;
    bool placeholder_mode = true;
    bool load_attempted = false;
    bool load_success = false;
    int input_width = 640;
    int input_height = 640;
    int input_channels = 3;
    int output_tensor_count = 0;
    float confidence_threshold = 0.5f;
    std::string load_message;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> class_names;
};

class YoloInference
{
public:
    static bool buildModelContext(int task_model_id, InferenceModelContext &context, std::string &error_message);
    static bool processFrame(FrameBuffer &render_frame,
                             const FrameBuffer &inference_frame,
                             const InferenceModelContext &context,
                             InferenceResult &result);
};

#endif // YOLO_INFERENCE_H
