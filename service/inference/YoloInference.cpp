#include "service/inference/YoloInference.h"

#include "service/model/ModelService.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <onnxruntime_cxx_api.h>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

namespace
{
struct CachedOnnxSession
{
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<int64_t> input_shape;
    std::vector<std::vector<int64_t>> output_shapes;
};

Ort::Env &getOrtEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "VisionInferYoloInference");
    return env;
}

bool pathExists(const std::string &path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

std::string shapeToString(const std::vector<int64_t> &shape)
{
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < shape.size(); ++i)
    {
        if (i > 0)
        {
            oss << ",";
        }
        oss << shape[i];
    }
    oss << "]";
    return oss.str();
}

const std::vector<std::string> &getCocoClassNames()
{
    static const std::vector<std::string> names = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
        "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
        "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant", "bed",
        "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
        "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"};
    return names;
}

float intersectionOverUnion(const DetectionBox &a, const DetectionBox &b)
{
    int left = std::max(a.left, b.left);
    int top = std::max(a.top, b.top);
    int right = std::min(a.right, b.right);
    int bottom = std::min(a.bottom, b.bottom);

    int inter_width = std::max(0, right - left);
    int inter_height = std::max(0, bottom - top);
    float inter_area = static_cast<float>(inter_width * inter_height);

    float area_a = static_cast<float>(std::max(0, a.right - a.left) * std::max(0, a.bottom - a.top));
    float area_b = static_cast<float>(std::max(0, b.right - b.left) * std::max(0, b.bottom - b.top));
    float union_area = area_a + area_b - inter_area;
    if (union_area <= 0.0f)
    {
        return 0.0f;
    }
    return inter_area / union_area;
}

std::vector<DetectionBox> applyNms(std::vector<DetectionBox> boxes, float iou_threshold)
{
    std::sort(boxes.begin(), boxes.end(), [](const DetectionBox &lhs, const DetectionBox &rhs)
              { return lhs.confidence > rhs.confidence; });

    std::vector<DetectionBox> kept;
    std::vector<bool> removed(boxes.size(), false);
    for (std::size_t i = 0; i < boxes.size(); ++i)
    {
        if (removed[i])
        {
            continue;
        }
        kept.push_back(boxes[i]);
        for (std::size_t j = i + 1; j < boxes.size(); ++j)
        {
            if (removed[j] || boxes[i].label != boxes[j].label)
            {
                continue;
            }
            if (intersectionOverUnion(boxes[i], boxes[j]) > iou_threshold)
            {
                removed[j] = true;
            }
        }
    }

    return kept;
}

bool getOrCreateOnnxSession(const std::string &model_path,
                            std::shared_ptr<CachedOnnxSession> &cached_session,
                            std::string &message)
{
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, std::shared_ptr<CachedOnnxSession>> cache;

    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = cache.find(model_path);
    if (it != cache.end())
    {
        cached_session = it->second;
        message = "ONNX Session 复用成功";
        return true;
    }

    try
    {
        auto holder = std::make_shared<CachedOnnxSession>();

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        holder->session = std::make_unique<Ort::Session>(getOrtEnv(), model_path.c_str(), session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        std::size_t input_count = holder->session->GetInputCount();
        std::size_t output_count = holder->session->GetOutputCount();

        for (std::size_t i = 0; i < input_count; ++i)
        {
            auto input_name = holder->session->GetInputNameAllocated(i, allocator);
            holder->input_names.emplace_back(input_name.get());

            Ort::TypeInfo type_info = holder->session->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            if (i == 0)
            {
                holder->input_shape = tensor_info.GetShape();
            }
        }

        for (std::size_t i = 0; i < output_count; ++i)
        {
            auto output_name = holder->session->GetOutputNameAllocated(i, allocator);
            holder->output_names.emplace_back(output_name.get());

            Ort::TypeInfo type_info = holder->session->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            holder->output_shapes.push_back(tensor_info.GetShape());
        }

        if (holder->input_shape.size() != 4)
        {
            message = "ONNX 模型输入维度不是 4D，当前仅支持 BCHW 图像输入";
            return false;
        }

        cache[model_path] = holder;
        cached_session = holder;
        message = "ONNX 模型加载成功，inputs=" + std::to_string(input_count) +
                  "，outputs=" + std::to_string(output_count) +
                  "，input_shape=" + shapeToString(holder->input_shape);
        return true;
    }
    catch (const Ort::Exception &ex)
    {
        message = std::string("ONNX 模型加载失败: ") + ex.what();
        return false;
    }
}

void fillInputTensorFromRgbFrame(const FrameBuffer &frame,
                                 int target_width,
                                 int target_height,
                                 std::vector<float> &tensor)
{
    int plane_size = target_width * target_height;
    tensor.assign(static_cast<std::size_t>(3 * plane_size), 0.0f);

    for (int y = 0; y < target_height; ++y)
    {
        int src_y = std::min(frame.height - 1, y * frame.height / target_height);
        const unsigned char *src_row = frame.data + src_y * frame.linesize;
        for (int x = 0; x < target_width; ++x)
        {
            int src_x = std::min(frame.width - 1, x * frame.width / target_width);
            const unsigned char *pixel = src_row + src_x * 3;
            int index = y * target_width + x;

            tensor[0 * plane_size + index] = static_cast<float>(pixel[0]) / 255.0f;
            tensor[1 * plane_size + index] = static_cast<float>(pixel[1]) / 255.0f;
            tensor[2 * plane_size + index] = static_cast<float>(pixel[2]) / 255.0f;
        }
    }
}

bool runOnnxForward(const FrameBuffer &frame,
                    const InferenceModelContext &context,
                    InferenceResult &result)
{
    std::shared_ptr<CachedOnnxSession> cached_session;
    std::string session_message;
    if (!getOrCreateOnnxSession(context.model_path, cached_session, session_message))
    {
        result.runtime_message = session_message;
        return false;
    }

    std::vector<int64_t> input_shape = cached_session->input_shape;
    input_shape[0] = 1;
    input_shape[1] = context.input_channels;
    input_shape[2] = context.input_height;
    input_shape[3] = context.input_width;

    std::vector<float> input_tensor_values;
    fillInputTensorFromRgbFrame(frame, context.input_width, context.input_height, input_tensor_values);

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info,
                                                              input_tensor_values.data(),
                                                              input_tensor_values.size(),
                                                              input_shape.data(),
                                                              input_shape.size());

    std::vector<const char *> input_names;
    for (const std::string &name : cached_session->input_names)
    {
        input_names.push_back(name.c_str());
    }

    std::vector<const char *> output_names;
    for (const std::string &name : cached_session->output_names)
    {
        output_names.push_back(name.c_str());
    }

    try
    {
        auto output_tensors = cached_session->session->Run(Ort::RunOptions{nullptr},
                                                           input_names.data(),
                                                           &input_tensor,
                                                           1,
                                                           output_names.data(),
                                                           output_names.size());

        result.real_inference_ran = true;
        result.output_tensor_count = static_cast<int>(output_tensors.size());

        std::ostringstream oss;
        oss << "真实前向推理成功，input_shape=" << shapeToString(input_shape);
        if (!cached_session->output_shapes.empty())
        {
            oss << "，first_output_shape=" << shapeToString(cached_session->output_shapes[0]);
        }
        oss << "，output_tensor_count=" << output_tensors.size();
        result.runtime_message = oss.str();
        return true;
    }
    catch (const Ort::Exception &ex)
    {
        result.runtime_message = std::string("真实前向推理失败: ") + ex.what();
        return false;
    }
}

bool decodeYoloV8Output(const Ort::Value &output_tensor,
                        const FrameBuffer &frame,
                        const InferenceModelContext &context,
                        InferenceResult &result)
{
    if (!output_tensor.IsTensor())
    {
        result.runtime_message += "；首个输出不是 Tensor";
        return false;
    }

    auto tensor_info = output_tensor.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> shape = tensor_info.GetShape();
    if (shape.size() != 3)
    {
        result.runtime_message += "；首个输出维度不是 3D";
        return false;
    }

    const float *data = output_tensor.GetTensorData<float>();
    if (data == nullptr)
    {
        result.runtime_message += "；首个输出数据为空";
        return false;
    }

    int features = 0;
    int candidates = 0;
    bool transposed_layout = false;
    if (shape[1] > 4 && shape[2] > 4)
    {
        // 常见 YOLOv8 导出： [1, 84, 8400]
        if (shape[1] <= shape[2])
        {
            features = static_cast<int>(shape[1]);
            candidates = static_cast<int>(shape[2]);
            transposed_layout = true;
        }
        else
        {
            features = static_cast<int>(shape[2]);
            candidates = static_cast<int>(shape[1]);
            transposed_layout = false;
        }
    }

    if (features < 5 || candidates <= 0)
    {
        result.runtime_message += "；未识别到可解析的 YOLOv8 输出布局";
        return false;
    }

    int class_count = features - 4;
    const std::vector<std::string> &class_names = getCocoClassNames();
    float scale_x = static_cast<float>(frame.width) / static_cast<float>(context.input_width);
    float scale_y = static_cast<float>(frame.height) / static_cast<float>(context.input_height);

    std::vector<DetectionBox> proposals;
    proposals.reserve(static_cast<std::size_t>(candidates));

    auto readValue = [&](int feature_index, int candidate_index) -> float
    {
        if (transposed_layout)
        {
            return data[feature_index * candidates + candidate_index];
        }
        return data[candidate_index * features + feature_index];
    };

    for (int i = 0; i < candidates; ++i)
    {
        float cx = readValue(0, i);
        float cy = readValue(1, i);
        float w = readValue(2, i);
        float h = readValue(3, i);

        int best_class_id = -1;
        float best_score = 0.0f;
        for (int cls = 0; cls < class_count; ++cls)
        {
            float score = readValue(4 + cls, i);
            if (score > best_score)
            {
                best_score = score;
                best_class_id = cls;
            }
        }

        if (best_class_id < 0 || best_score < context.confidence_threshold)
        {
            continue;
        }

        float left = (cx - w * 0.5f) * scale_x;
        float top = (cy - h * 0.5f) * scale_y;
        float right = (cx + w * 0.5f) * scale_x;
        float bottom = (cy + h * 0.5f) * scale_y;

        DetectionBox box;
        box.left = std::max(0, static_cast<int>(std::floor(left)));
        box.top = std::max(0, static_cast<int>(std::floor(top)));
        box.right = std::min(frame.width - 1, static_cast<int>(std::ceil(right)));
        box.bottom = std::min(frame.height - 1, static_cast<int>(std::ceil(bottom)));
        box.confidence = best_score;
        if (best_class_id >= 0 && best_class_id < static_cast<int>(class_names.size()))
        {
            box.label = class_names[best_class_id];
        }
        else
        {
            box.label = "class_" + std::to_string(best_class_id);
        }

        if (box.right > box.left && box.bottom > box.top)
        {
            proposals.push_back(box);
        }
    }

    std::vector<DetectionBox> detections = applyNms(proposals, 0.45f);
    result.boxes = detections;
    result.detection_count = static_cast<int>(detections.size());

    std::ostringstream oss;
    oss << result.runtime_message
        << "，decoded_candidates=" << candidates
        << "，class_count=" << class_count
        << "，detections_after_nms=" << detections.size();
    result.runtime_message = oss.str();
    return true;
}

void drawDetectionBox(FrameBuffer &frame, const DetectionBox &box)
{
    int thickness = 4;
    for (int y = 0; y < frame.height; ++y)
    {
        unsigned char *row = frame.data + y * frame.linesize;
        for (int x = 0; x < frame.width; ++x)
        {
            bool is_top_or_bottom = (y >= box.top && y < box.top + thickness) ||
                                    (y <= box.bottom && y > box.bottom - thickness);
            bool is_left_or_right = (x >= box.left && x < box.left + thickness) ||
                                    (x <= box.right && x > box.right - thickness);
            bool inside_vertical = (y >= box.top && y <= box.bottom);
            bool inside_horizontal = (x >= box.left && x <= box.right);

            if ((is_top_or_bottom && inside_horizontal) || (is_left_or_right && inside_vertical))
            {
                // RGB24: red bounding box.
                row[x * 3 + 0] = 255;
                row[x * 3 + 1] = 0;
                row[x * 3 + 2] = 0;
            }
        }
    }
}
}

bool YoloInference::buildModelContext(int task_model_id, InferenceModelContext &context, std::string &error_message)
{
    ModelEntity model;
    bool found_model = false;

    if (task_model_id > 0)
    {
        found_model = ModelService::getModelById(task_model_id, model);
        if (!found_model)
        {
            error_message = "任务绑定的模型不存在";
            return false;
        }
        context.uses_active_model = false;
    }
    else
    {
        found_model = ModelService::getCurrentActiveModel(model);
        context.uses_active_model = true;
    }

    if (!found_model)
    {
        // 当前阶段允许没有真实模型时继续跑占位推理，这样不会阻塞主链路联调。
        context.model_id = 0;
        context.model_name = "placeholder_model";
        context.model_path.clear();
        context.framework = "placeholder";
        context.file_exists = false;
        context.placeholder_mode = true;
        context.metadata["source"] = "fallback_without_db_model";
        error_message.clear();
        return true;
    }

    context.model_id = model.id;
    context.model_name = model.model_name;
    context.model_path = model.file_path;
    context.framework = model.framework.empty() ? "onnx" : model.framework;
    context.file_exists = pathExists(context.model_path);
    context.placeholder_mode = true;
    context.load_attempted = false;
    context.load_success = false;
    context.load_message.clear();
    context.metadata["source"] = context.uses_active_model ? "active_model" : "task_model";
    context.metadata["uploaded_by"] = model.uploaded_by;

    if (context.framework == "onnx" && context.file_exists)
    {
        context.load_attempted = true;
        std::shared_ptr<CachedOnnxSession> cached_session;
        context.load_success = getOrCreateOnnxSession(context.model_path, cached_session, context.load_message);
        context.placeholder_mode = !context.load_success;
    }
    else if (context.framework == "onnx")
    {
        context.load_attempted = true;
        context.load_success = false;
        context.load_message = "模型文件不存在，暂时回退到占位推理";
    }
    else
    {
        context.load_message = "当前仅对 onnx 框架做加载探测，其余框架暂时回退到占位推理";
    }

    context.metadata["load_attempted"] = context.load_attempted ? "true" : "false";
    context.metadata["load_success"] = context.load_success ? "true" : "false";
    if (context.load_success)
    {
        std::shared_ptr<CachedOnnxSession> cached_session;
        std::string ignored_message;
        if (getOrCreateOnnxSession(context.model_path, cached_session, ignored_message))
        {
            if (cached_session->input_shape.size() == 4)
            {
                context.input_channels = static_cast<int>(cached_session->input_shape[1] > 0 ? cached_session->input_shape[1] : 3);
                context.input_height = static_cast<int>(cached_session->input_shape[2] > 0 ? cached_session->input_shape[2] : 640);
                context.input_width = static_cast<int>(cached_session->input_shape[3] > 0 ? cached_session->input_shape[3] : 640);
            }
            context.output_tensor_count = static_cast<int>(cached_session->output_names.size());
            context.metadata["input_shape"] = shapeToString(cached_session->input_shape);
            if (!cached_session->output_shapes.empty())
            {
                context.metadata["first_output_shape"] = shapeToString(cached_session->output_shapes[0]);
            }
        }
    }

    // 即使模型加载失败，也先不阻断整个视频任务，方便继续联调整体链路。
    error_message.clear();
    return true;
}

bool YoloInference::processFrame(FrameBuffer &frame, const InferenceModelContext &context, InferenceResult &result)
{
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0)
    {
        return false;
    }

    bool forward_success = false;
    if (context.load_success && context.framework == "onnx")
    {
        forward_success = runOnnxForward(frame, context, result);
    }

    result.model_name = context.model_name;
    result.model_framework = context.framework;
    bool decode_success = false;
    if (forward_success && result.output_tensor_count > 0)
    {
        std::shared_ptr<CachedOnnxSession> cached_session;
        std::string ignored_message;
        if (getOrCreateOnnxSession(context.model_path, cached_session, ignored_message))
        {
            std::vector<int64_t> input_shape = cached_session->input_shape;
            input_shape[0] = 1;
            input_shape[1] = context.input_channels;
            input_shape[2] = context.input_height;
            input_shape[3] = context.input_width;

            std::vector<float> input_tensor_values;
            fillInputTensorFromRgbFrame(frame, context.input_width, context.input_height, input_tensor_values);
            Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info,
                                                                      input_tensor_values.data(),
                                                                      input_tensor_values.size(),
                                                                      input_shape.data(),
                                                                      input_shape.size());
            std::vector<const char *> input_names;
            for (const std::string &name : cached_session->input_names)
            {
                input_names.push_back(name.c_str());
            }
            std::vector<const char *> output_names;
            for (const std::string &name : cached_session->output_names)
            {
                output_names.push_back(name.c_str());
            }
            try
            {
                auto output_tensors = cached_session->session->Run(Ort::RunOptions{nullptr},
                                                                   input_names.data(),
                                                                   &input_tensor,
                                                                   1,
                                                                   output_names.data(),
                                                                   output_names.size());
                if (!output_tensors.empty())
                {
                    decode_success = decodeYoloV8Output(output_tensors[0], frame, context, result);
                }
            }
            catch (const Ort::Exception &ex)
            {
                result.runtime_message += std::string("；后处理阶段重新执行 Run 失败: ") + ex.what();
            }
        }
    }

    if (decode_success && !result.boxes.empty())
    {
        for (const DetectionBox &box : result.boxes)
        {
            drawDetectionBox(frame, box);
        }
    }
    else if (!forward_success)
    {
        DetectionBox box;
        box.left = frame.width / 3;
        box.top = frame.height / 3;
        box.right = box.left + frame.width / 3;
        box.bottom = box.top + frame.height / 3;
        box.label = "placeholder_target";
        box.confidence = 0.85f;
        drawDetectionBox(frame, box);
        result.detection_count = 1;
        result.boxes.clear();
        result.boxes.push_back(box);
    }

    std::ostringstream summary;
    summary << (decode_success ? "YOLOv8 后处理完成，已生成真实检测框；模型=" :
                                 (forward_success ? "真实前向已执行，但当前未解析出有效检测框；模型=" :
                                                    "占位推理完成，已对当前帧绘制占位检测框；模型="))
            << context.model_name
            << "；框架=" << context.framework
            << "；模型文件" << (context.file_exists ? "存在" : "暂未找到，当前继续走占位模式")
            << "；加载状态=" << (context.load_attempted ? (context.load_success ? "加载成功" : "加载失败") : "未尝试加载")
            << "；检测框数量=" << result.detection_count
            << "；运行信息=" << (result.runtime_message.empty() ? "无" : result.runtime_message);
    result.summary = summary.str();
    return true;
}
