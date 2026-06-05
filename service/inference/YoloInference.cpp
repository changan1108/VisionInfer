#include "service/inference/YoloInference.h"

#include "common/config/AppConfig.h"
#include "service/model/ModelService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <memory>
#include <mutex>
#include <onnxruntime_cxx_api.h>
#include <sstream>
#include <thread>
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
    std::vector<const char *> input_name_ptrs;
    std::vector<const char *> output_name_ptrs;
    std::vector<int64_t> input_shape;
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<std::string> class_names;
};

struct ForwardPassResult
{
    bool success = false;
    std::vector<Ort::Value> output_tensors;
    std::string runtime_message;
    int output_tensor_count = 0;
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

std::string trimCopy(const std::string &input)
{
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0)
    {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
    {
        --end;
    }
    return input.substr(start, end - start);
}

std::vector<std::string> parseClassNamesMetadata(const std::string &raw_value)
{
    std::vector<std::string> class_names;
    std::string compact = trimCopy(raw_value);
    if (compact.empty())
    {
        return class_names;
    }

    if (!compact.empty() && compact.front() == '{' && compact.back() == '}')
    {
        compact = compact.substr(1, compact.size() - 2);
    }
    else if (!compact.empty() && compact.front() == '[' && compact.back() == ']')
    {
        compact = compact.substr(1, compact.size() - 2);
    }

    std::vector<std::pair<int, std::string>> indexed_names;
    std::size_t cursor = 0;
    while (cursor < compact.size())
    {
        std::size_t comma_pos = compact.find(',', cursor);
        std::string token = compact.substr(cursor, comma_pos == std::string::npos ? std::string::npos : comma_pos - cursor);
        token = trimCopy(token);
        if (!token.empty())
        {
            std::size_t colon_pos = token.find(':');
            if (colon_pos != std::string::npos)
            {
                std::string index_part = trimCopy(token.substr(0, colon_pos));
                std::string name_part = trimCopy(token.substr(colon_pos + 1));
                if (!name_part.empty() &&
                    ((name_part.front() == '\'' && name_part.back() == '\'') ||
                     (name_part.front() == '"' && name_part.back() == '"')))
                {
                    name_part = name_part.substr(1, name_part.size() - 2);
                }
                try
                {
                    int index = std::stoi(index_part);
                    indexed_names.push_back(std::make_pair(index, name_part));
                }
                catch (...)
                {
                }
            }
            else
            {
                if ((token.front() == '\'' && token.back() == '\'') ||
                    (token.front() == '"' && token.back() == '"'))
                {
                    token = token.substr(1, token.size() - 2);
                }
                class_names.push_back(token);
            }
        }

        if (comma_pos == std::string::npos)
        {
            break;
        }
        cursor = comma_pos + 1;
    }

    if (!indexed_names.empty())
    {
        std::sort(indexed_names.begin(), indexed_names.end());
        class_names.assign(static_cast<std::size_t>(indexed_names.back().first + 1), "");
        for (const std::pair<int, std::string> &item : indexed_names)
        {
            if (item.first >= 0)
            {
                class_names[static_cast<std::size_t>(item.first)] = item.second;
            }
        }
        while (!class_names.empty() && class_names.back().empty())
        {
            class_names.pop_back();
        }
    }

    return class_names;
}

const std::unordered_map<char, std::array<unsigned char, 7>> &getBitmapFont()
{
    static const std::unordered_map<char, std::array<unsigned char, 7>> font = {
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}},
        {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F}},
        {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
        {'J', {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
        {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
        {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
        {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
        {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
        {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
        {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
        {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
        {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
        {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
        {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}};
    return font;
}

void setPixel(FrameBuffer &frame, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height)
    {
        return;
    }
    unsigned char *pixel = frame.data + static_cast<std::ptrdiff_t>(y) * frame.linesize + static_cast<std::ptrdiff_t>(x) * 3;
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
}

void fillRect(FrameBuffer &frame, int left, int top, int right, int bottom, unsigned char r, unsigned char g, unsigned char b)
{
    int clamped_left = std::max(0, left);
    int clamped_top = std::max(0, top);
    int clamped_right = std::min(frame.width - 1, right);
    int clamped_bottom = std::min(frame.height - 1, bottom);
    for (int y = clamped_top; y <= clamped_bottom; ++y)
    {
        for (int x = clamped_left; x <= clamped_right; ++x)
        {
            setPixel(frame, x, y, r, g, b);
        }
    }
}

void drawBitmapChar(FrameBuffer &frame,
                    int origin_x,
                    int origin_y,
                    char ch,
                    int scale,
                    unsigned char r,
                    unsigned char g,
                    unsigned char b)
{
    char normalized = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const auto &font = getBitmapFont();
    auto it = font.find(normalized);
    if (it == font.end())
    {
        it = font.find(' ');
    }

    const std::array<unsigned char, 7> &glyph = it->second;
    for (int row = 0; row < 7; ++row)
    {
        for (int col = 0; col < 5; ++col)
        {
            if ((glyph[static_cast<std::size_t>(row)] >> (4 - col)) & 0x01U)
            {
                for (int dy = 0; dy < scale; ++dy)
                {
                    for (int dx = 0; dx < scale; ++dx)
                    {
                        setPixel(frame,
                                 origin_x + col * scale + dx,
                                 origin_y + row * scale + dy,
                                 r,
                                 g,
                                 b);
                    }
                }
            }
        }
    }
}

void drawText(FrameBuffer &frame,
              int origin_x,
              int origin_y,
              const std::string &text,
              int scale,
              unsigned char r,
              unsigned char g,
              unsigned char b)
{
    int x = origin_x;
    for (char ch : text)
    {
        drawBitmapChar(frame, x, origin_y, ch, scale, r, g, b);
        x += 6 * scale;
    }
}

std::string formatDetectionCaption(const DetectionBox &box)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << box.label << " " << box.confidence;
    return oss.str();
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
        int intra_threads = AppConfig::ONNX_INTRA_OP_THREADS > 0
                                ? AppConfig::ONNX_INTRA_OP_THREADS
                                : static_cast<int>(std::max(1u, std::thread::hardware_concurrency() / 2));
        int inter_threads = AppConfig::ONNX_INTER_OP_THREADS > 0 ? AppConfig::ONNX_INTER_OP_THREADS : 1;
        session_options.SetIntraOpNumThreads(intra_threads);
        session_options.SetInterOpNumThreads(inter_threads);
        session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        holder->session = std::make_unique<Ort::Session>(getOrtEnv(), model_path.c_str(), session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        std::size_t input_count = holder->session->GetInputCount();
        std::size_t output_count = holder->session->GetOutputCount();

        for (std::size_t i = 0; i < input_count; ++i)
        {
            auto input_name = holder->session->GetInputNameAllocated(i, allocator);
            holder->input_names.emplace_back(input_name.get());
            holder->input_name_ptrs.push_back(holder->input_names.back().c_str());

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
            holder->output_name_ptrs.push_back(holder->output_names.back().c_str());

            Ort::TypeInfo type_info = holder->session->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            holder->output_shapes.push_back(tensor_info.GetShape());
        }

        Ort::ModelMetadata metadata = holder->session->GetModelMetadata();
        std::vector<Ort::AllocatedStringPtr> metadata_keys = metadata.GetCustomMetadataMapKeysAllocated(allocator);
        for (const auto &key_ptr : metadata_keys)
        {
            const char *key_cstr = key_ptr.get();
            if (key_cstr == nullptr)
            {
                continue;
            }
            std::string key = key_cstr;
            if (key == "names" || key == "classes")
            {
                auto value_ptr = metadata.LookupCustomMetadataMapAllocated(key.c_str(), allocator);
                if (value_ptr)
                {
                    holder->class_names = parseClassNamesMetadata(value_ptr.get());
                }
            }
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

struct ResizeIndexCache
{
    int source_width = 0;
    int source_height = 0;
    int target_width = 0;
    int target_height = 0;
    std::vector<int> x_map;
    std::vector<int> y_map;
};

ResizeIndexCache &getResizeIndexCache(int source_width, int source_height, int target_width, int target_height)
{
    thread_local ResizeIndexCache cache;
    if (cache.source_width == source_width &&
        cache.source_height == source_height &&
        cache.target_width == target_width &&
        cache.target_height == target_height)
    {
        return cache;
    }

    cache.source_width = source_width;
    cache.source_height = source_height;
    cache.target_width = target_width;
    cache.target_height = target_height;
    cache.x_map.resize(static_cast<std::size_t>(target_width));
    cache.y_map.resize(static_cast<std::size_t>(target_height));
    for (int x = 0; x < target_width; ++x)
    {
        cache.x_map[static_cast<std::size_t>(x)] = std::min(source_width - 1, x * source_width / target_width);
    }
    for (int y = 0; y < target_height; ++y)
    {
        cache.y_map[static_cast<std::size_t>(y)] = std::min(source_height - 1, y * source_height / target_height);
    }
    return cache;
}

void fillInputTensorFromRgbFrame(const FrameBuffer &frame,
                                 int target_width,
                                 int target_height,
                                 std::vector<float> &tensor)
{
    int plane_size = target_width * target_height;
    std::size_t tensor_size = static_cast<std::size_t>(3 * plane_size);
    if (tensor.size() != tensor_size)
    {
        tensor.resize(tensor_size);
    }

    ResizeIndexCache &cache = getResizeIndexCache(frame.width, frame.height, target_width, target_height);
    float *plane_r = tensor.data();
    float *plane_g = plane_r + plane_size;
    float *plane_b = plane_g + plane_size;

    for (int y = 0; y < target_height; ++y)
    {
        int src_y = cache.y_map[static_cast<std::size_t>(y)];
        const unsigned char *src_row = frame.data + static_cast<std::ptrdiff_t>(src_y) * frame.linesize;
        int row_offset = y * target_width;
        for (int x = 0; x < target_width; ++x)
        {
            int src_x = cache.x_map[static_cast<std::size_t>(x)];
            const unsigned char *pixel = src_row + static_cast<std::ptrdiff_t>(src_x) * 3;
            int index = row_offset + x;

            plane_r[index] = static_cast<float>(pixel[0]) * (1.0f / 255.0f);
            plane_g[index] = static_cast<float>(pixel[1]) * (1.0f / 255.0f);
            plane_b[index] = static_cast<float>(pixel[2]) * (1.0f / 255.0f);
        }
    }
}

bool runOnnxForward(const FrameBuffer &frame,
                    const InferenceModelContext &context,
                    ForwardPassResult &forward_result)
{
    std::shared_ptr<CachedOnnxSession> cached_session;
    std::string session_message;
    if (!getOrCreateOnnxSession(context.model_path, cached_session, session_message))
    {
        forward_result.runtime_message = session_message;
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

    try
    {
        auto output_tensors = cached_session->session->Run(Ort::RunOptions{nullptr},
                                                           cached_session->input_name_ptrs.data(),
                                                           &input_tensor,
                                                           1,
                                                           cached_session->output_name_ptrs.data(),
                                                           cached_session->output_name_ptrs.size());

        forward_result.success = true;
        forward_result.output_tensor_count = static_cast<int>(output_tensors.size());
        forward_result.output_tensors = std::move(output_tensors);

        std::ostringstream oss;
        oss << "真实前向推理成功，input_shape=" << shapeToString(input_shape);
        if (!cached_session->output_shapes.empty())
        {
            oss << "，first_output_shape=" << shapeToString(cached_session->output_shapes[0]);
        }
        oss << "，output_tensor_count=" << forward_result.output_tensor_count;
        forward_result.runtime_message = oss.str();
        return true;
    }
    catch (const Ort::Exception &ex)
    {
        forward_result.runtime_message = std::string("真实前向推理失败: ") + ex.what();
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
    const std::vector<std::string> &class_names = context.class_names.empty()
                                                      ? getCocoClassNames()
                                                      : context.class_names;
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

    std::string caption = formatDetectionCaption(box);
    const int text_scale = 3;
    int caption_width = static_cast<int>(caption.size()) * 6 * text_scale - text_scale;
    int caption_height = 7 * text_scale + 4;
    int caption_left = std::max(0, box.left);
    int caption_top = std::max(0, box.top - caption_height - 2);
    int caption_right = std::min(frame.width - 1, caption_left + caption_width + 3);
    int caption_bottom = std::min(frame.height - 1, caption_top + caption_height);

    fillRect(frame, caption_left, caption_top, caption_right, caption_bottom, 255, 0, 0);
    drawText(frame, caption_left + 2, caption_top + 2, caption, text_scale, 255, 255, 255);
}
}

// 构建推理模型上下文
bool YoloInference::buildModelContext(int task_model_id, InferenceModelContext &context, std::string &error_message)
{
    ModelEntity model;
    bool found_model = false;

    // 如果任务指定了model_id，查询指定模型
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
    else //否则查询当前激活模型
    {
        found_model = ModelService::getCurrentActiveModel(model);
        context.uses_active_model = true;
    }

    // 测试使用
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

    // 获取模型名称、路径和框架
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

    // ONNX 模型且文件存在。
    // 尝试从缓存中复用 Session；若缓存没有，则创建并缓存新的 ONNX Runtime Session。
    if (context.framework == "onnx" && context.file_exists)
    {
        context.load_attempted = true;
        std::shared_ptr<CachedOnnxSession> cached_session;
        context.load_success = getOrCreateOnnxSession(context.model_path, cached_session, context.load_message);
        // Session 加载成功时使用真实 ONNX 推理；失败时退回占位推理。
        context.placeholder_mode = !context.load_success;
    }
    // 框架声明为 ONNX，但模型文件不存在。
    // 此时无法创建 Session，只记录失败原因并保留占位推理模式。
    else if (context.framework == "onnx")
    {
        context.load_attempted = true;
        context.load_success = false;
        context.load_message = "模型文件不存在，暂时回退到占位推理";
    }
    // 不是ONNX模型,当前实现不加载其他框架，直接使用占位推理。
    // 注意此时不进行ONNX检查/驳回，因为这个业务属于"ModelUploadService::uploadModel"接口
    else
    {
        context.load_message = "当前仅对 onnx 框架做加载探测，其余框架暂时回退到占位推理";
    }

    // 将加载结果保存到 metadata，供任务结果摘要和系统状态展示使用。
    context.metadata["load_attempted"] = context.load_attempted ? "true" : "false";
    context.metadata["load_success"] = context.load_success ? "true" : "false";

    // Session 加载成功后，在这里读取模型元信息，并不执行前向推理。
    // 真正的 ONNX 前向推理发生在 processFrame() -> runOnnxForward() 中。
    if (context.load_success)
    {
        std::shared_ptr<CachedOnnxSession> cached_session;
        std::string ignored_message;
        // 再次调用会命中缓存，用于取得已加载 Session 的输入输出描述。
        if (getOrCreateOnnxSession(context.model_path, cached_session, ignored_message))
        {
            // 常见模型输入形状为 NCHW：[批大小, 通道数, 高度, 宽度]。
            if (cached_session->input_shape.size() == 4)
            {
                context.input_channels = static_cast<int>(cached_session->input_shape[1] > 0 ? cached_session->input_shape[1] : 3);
                context.input_height = static_cast<int>(cached_session->input_shape[2] > 0 ? cached_session->input_shape[2] : 640);
                context.input_width = static_cast<int>(cached_session->input_shape[3] > 0 ? cached_session->input_shape[3] : 640);
            }
            // 保存输出张量数量、类别名称和张量形状，供后续预处理及 YOLO 后处理使用。
            context.output_tensor_count = static_cast<int>(cached_session->output_names.size());
            context.class_names = cached_session->class_names;
            context.metadata["input_shape"] = shapeToString(cached_session->input_shape);
            if (!cached_session->output_shapes.empty())
            {
                context.metadata["first_output_shape"] = shapeToString(cached_session->output_shapes[0]);
            }
            if (!context.class_names.empty())
            {
                context.metadata["class_count"] = std::to_string(context.class_names.size());
            }
        }
    }


    error_message.clear();
    return true;
}

// YOLO/ONNX 单帧处理
bool YoloInference::processFrame(FrameBuffer &render_frame,
                                 const FrameBuffer &inference_frame,
                                 const InferenceModelContext &context,
                                 InferenceResult &result)
{
    if (render_frame.data == nullptr || render_frame.width <= 0 || render_frame.height <= 0)
    {
        return false;
    }

    if (inference_frame.data == nullptr || inference_frame.width <= 0 || inference_frame.height <= 0)
    {
        return false;
    }

    bool forward_success = false;
    ForwardPassResult forward_result;
    if (context.load_success && context.framework == "onnx")
    {
        auto forward_started_at = std::chrono::steady_clock::now();
        // runOnnxForward 执行 ONNX 前向推理
        forward_success = runOnnxForward(inference_frame, context, forward_result);
        auto forward_finished_at = std::chrono::steady_clock::now();
        result.forward_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(forward_finished_at - forward_started_at).count());
    }

    result.model_name = context.model_name;
    result.model_framework = context.framework;
    result.real_inference_ran = forward_success;
    result.output_tensor_count = forward_result.output_tensor_count;
    result.runtime_message = forward_result.runtime_message;
    bool decode_success = false;
    if (forward_success && !forward_result.output_tensors.empty())
    {
        auto postprocess_started_at = std::chrono::steady_clock::now();
        // decodeYoloV8Output 后处理
        decode_success = decodeYoloV8Output(forward_result.output_tensors[0], render_frame, context, result);
        auto postprocess_finished_at = std::chrono::steady_clock::now();
        result.postprocess_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(postprocess_finished_at - postprocess_started_at).count());
    }

    if (decode_success && !result.boxes.empty())
    {
        auto draw_started_at = std::chrono::steady_clock::now();
        for (const DetectionBox &box : result.boxes)
        {
            // 在原尺寸帧上画框
            drawDetectionBox(render_frame, box);
        }
        auto draw_finished_at = std::chrono::steady_clock::now();
        result.draw_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(draw_finished_at - draw_started_at).count());
    }
    else if (!forward_success)
    {
        DetectionBox box;
        box.left = render_frame.width / 3;
        box.top = render_frame.height / 3;
        box.right = box.left + render_frame.width / 3;
        box.bottom = box.top + render_frame.height / 3;
        box.label = "placeholder_target";
        box.confidence = 0.85f;
        auto draw_started_at = std::chrono::steady_clock::now();
        drawDetectionBox(render_frame, box);
        auto draw_finished_at = std::chrono::steady_clock::now();
        result.draw_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(draw_finished_at - draw_started_at).count());
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
