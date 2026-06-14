#ifndef YOLO_INFERENCE_H
#define YOLO_INFERENCE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// 检测框
struct DetectionBox
{
    int left = 0;// 走上角坐标
    int top = 0;
    int right = 0;// 右下角坐标
    int bottom = 0;
    std::string label;// 类别
    float confidence = 0.0f;// 置信度
};

struct FrameBuffer
{
    int width = 0; // 图像尺寸
    int height = 0;
    int linesize = 0; // 一行像素数据实际占用的字节数
    unsigned char *data = nullptr; // RGB 像素内存首地址
};

struct InferenceResult
{
    int detection_count = 0;
    std::vector<DetectionBox> boxes;// 最终通过筛选的检测框
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

// 是一次视频任务进行推理时所需的模型运行配置和加载状态集合
struct InferenceModelContext
{
    int model_id = 0;                            // 数据库 models 表中的模型 ID；0 表示没有绑定真实模型。
    std::string model_name;                      // 模型名称，用于任务结果记录和前端展示。
    std::string model_path;                      // 模型文件在服务器上的完整存储路径。
    std::string framework;                       // 模型框架类型；当前真实推理主要支持 onnx。
    bool file_exists = false;                    // model_path 指向的模型文件在磁盘上是否真实存在。
    bool uses_active_model = false;              // true 表示任务未指定模型 ID，使用系统当前激活模型。
    bool placeholder_mode = true;                // true 表示不能执行真实推理，后续使用占位检测框降级。
    bool load_attempted = false;                 // 是否已经尝试创建或获取 ONNX Runtime Session。
    bool load_success = false;                   // ONNX Runtime Session 是否加载成功，可否执行真实前向推理。
    int input_width = 640;                       // 模型输入图像宽度；默认按 YOLO 常用的 640 处理。
    int input_height = 640;                      // 模型输入图像高度。
    int input_channels = 3;                      // 模型输入通道数；RGB 图像通常为 3。
    int output_tensor_count = 0;                 // 模型推理输出张量的数量。
    float confidence_threshold = 0.5f;           // 检测框置信度过滤阈值。
    std::string load_message;                    // 模型加载成功、失败或降级原因的可读说明。
    std::map<std::string, std::string> metadata; // 输入/输出形状、模型来源、上传者等扩展元数据。
    std::vector<std::string> class_names;        // 类别 ID 对应的目标类别名称。
};

class YoloInference
{
public:
    // 构建推理模型上下文
    static bool buildModelContext(int task_model_id, InferenceModelContext &context, std::string &error_message);
    
    // YOLO/ONNX 单帧处理
    static bool processFrame(FrameBuffer &render_frame,
                             const FrameBuffer &inference_frame,
                             const InferenceModelContext &context,
                             InferenceResult &result);
};

#endif // YOLO_INFERENCE_H
