#include "service/model/ModelUploadService.h"

#include "common/config/AppConfig.h"
#include "service/model/ModelService.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <onnxruntime_cxx_api.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace
{
bool pathExists(const std::string &path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

bool createDirectoryIfNeeded(const std::string &path)
{
    if (path.empty())
    {
        return false;
    }

    if (pathExists(path))
    {
        return true;
    }

    std::size_t pos = 0;
    while (pos < path.size())
    {
        pos = path.find('/', pos);
        std::string current = path.substr(0, pos);
        if (!current.empty() && !pathExists(current))
        {
            if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
            {
                return false;
            }
        }

        if (pos == std::string::npos)
        {
            break;
        }
        ++pos;
    }

    return pathExists(path);
}

std::string sanitizeFilename(const std::string &filename)
{
    std::string sanitized;
    for (std::size_t i = 0; i < filename.size(); ++i)
    {
        unsigned char ch = static_cast<unsigned char>(filename[i]);
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-')
        {
            sanitized.push_back(static_cast<char>(ch));
        }
        else
        {
            sanitized.push_back('_');
        }
    }

    if (sanitized.empty())
    {
        sanitized = "uploaded_model.onnx";
    }
    return sanitized;
}

std::string toLowerCopy(const std::string &input)
{
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

std::string getFileExtension(const std::string &filename)
{
    std::size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos)
    {
        return "";
    }
    return toLowerCopy(filename.substr(dot_pos));
}

std::string buildStoredFilename(const std::string &model_name, const std::string &extension)
{
    auto now = std::chrono::system_clock::now();
    long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string safe_name = sanitizeFilename(model_name);
    return std::to_string(millis) + "_" + safe_name + extension;
}

bool writeBinaryFile(const std::string &path, const std::string &content)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output.is_open())
    {
        return false;
    }

    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    return output.good();
}

bool validateOnnxModel(const std::string &path, std::string &error_message)
{
    try
    {
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "VisionInferModelUpload");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        Ort::Session session(env, path.c_str(), session_options);
        Ort::AllocatorWithDefaultOptions allocator;
        std::size_t input_count = session.GetInputCount();
        if (input_count == 0)
        {
            error_message = "ONNX 模型没有输入节点";
            return false;
        }

        Ort::TypeInfo type_info = session.GetInputTypeInfo(0);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> input_shape = tensor_info.GetShape();
        if (input_shape.size() != 4)
        {
            error_message = "ONNX 模型输入维度不是 4D，当前仅支持 BCHW 图像输入";
            return false;
        }

        error_message.clear();
        return true;
    }
    catch (const Ort::Exception &ex)
    {
        error_message = std::string("ONNX 模型加载失败: ") + ex.what();
        return false;
    }
}
}

bool ModelUploadService::uploadModel(const ModelUploadRequest &request, ModelUploadResult &result,
                                     std::string &error_message)
{
    if (request.model_name.empty())
    {
        error_message = "model_name 为必填项";
        return false;
    }

    if (request.file_content.empty())
    {
        error_message = "模型文件内容不能为空";
        return false;
    }

    // 检查是否是onnx模型
    std::string framework = request.framework.empty() ? "onnx" : toLowerCopy(request.framework);
    if (framework != "onnx")
    {
        error_message = "当前仅支持上传 onnx 模型";
        return false;
    }

    std::string original_filename = sanitizeFilename(request.original_filename.empty() ? "uploaded_model.onnx"
                                                                                       : request.original_filename);
    std::string extension = getFileExtension(original_filename);
    
    // 检查扩展名
    if (extension != ".onnx")
    {
        error_message = "当前仅支持 .onnx 模型文件";
        return false;
    }

    if (!createDirectoryIfNeeded(AppConfig::MODEL_STORAGE_DIR))
    {
        error_message = "创建模型存储目录失败";
        return false;
    }

    result.stored_filename = buildStoredFilename(request.model_name, extension);
    std::string stored_path = std::string(AppConfig::MODEL_STORAGE_DIR) + "/" + result.stored_filename;

    if (!writeBinaryFile(stored_path, request.file_content))
    {
        error_message = "模型文件保存失败";
        return false;
    }

    if (!validateOnnxModel(stored_path, error_message))
    {
        std::remove(stored_path.c_str());
        return false;
    }

    ModelEntity model;
    model.model_name = request.model_name;
    model.file_path = stored_path;
    model.framework = framework;
    model.uploaded_by = request.uploaded_by;
    model.is_active = request.is_active;

    if (!ModelService::addModel(model, error_message))
    {
        std::remove(stored_path.c_str());
        return false;
    }

    result.model = model;
    return true;
}
