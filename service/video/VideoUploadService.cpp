#include "service/video/VideoUploadService.h"

#include "common/config/AppConfig.h"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace
{
std::string getBaseName(const std::string &path)
{
    std::size_t slash_pos = path.find_last_of("/\\");
    if (slash_pos == std::string::npos)
    {
        return path;
    }
    return path.substr(slash_pos + 1);
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
        sanitized = "uploaded_video.mp4";
    }
    return sanitized;
}

bool pathExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
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

long long getFileSize(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        return 0;
    }
    return static_cast<long long>(st.st_size);
}

std::string buildStoredFilename(const std::string &filename)
{
    auto now = std::chrono::system_clock::now();
    long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(millis) + "_" + filename;
}
}

bool VideoUploadService::uploadFromServerPath(const VideoUploadRequest &request, VideoUploadResult &result,
                                              std::string &error_message)
{
    if (request.submitted_by.empty() || request.source_file_path.empty())
    {
        error_message = "submitted_by 和 source_file_path 为必填项";
        return false;
    }

    std::ifstream input(request.source_file_path.c_str(), std::ios::binary);
    if (!input.is_open())
    {
        error_message = "无法打开 source_file_path 指向的源文件";
        return false;
    }

    if (!createDirectoryIfNeeded(AppConfig::VIDEO_INPUT_DIR))
    {
        error_message = "创建视频输入目录失败";
        return false;
    }

    result.original_filename = request.original_filename.empty() ? getBaseName(request.source_file_path)
                                                                 : request.original_filename;
    result.original_filename = sanitizeFilename(result.original_filename);
    result.stored_filename = buildStoredFilename(result.original_filename);
    result.stored_path = std::string(AppConfig::VIDEO_INPUT_DIR) + "/" + result.stored_filename;

    std::ofstream output(result.stored_path.c_str(), std::ios::binary);
    if (!output.is_open())
    {
        error_message = "无法创建目标视频文件";
        return false;
    }

    output << input.rdbuf();
    output.flush();

    if (!output.good())
    {
        error_message = "写入目标视频文件失败";
        return false;
    }

    result.file_size_bytes = getFileSize(result.stored_path);
    return true;
}

bool VideoUploadService::uploadFromBinary(const VideoUploadRequest &request, VideoUploadResult &result,
                                          std::string &error_message)
{
    if (request.submitted_by.empty() || request.file_content.empty())
    {
        error_message = "submitted_by 和上传文件内容为必填项";
        return false;
    }

    if (!createDirectoryIfNeeded(AppConfig::VIDEO_INPUT_DIR))
    {
        error_message = "创建视频输入目录失败";
        return false;
    }

    result.original_filename = sanitizeFilename(request.original_filename.empty() ? "uploaded_video.mp4"
                                                                                  : request.original_filename);
    result.stored_filename = buildStoredFilename(result.original_filename);
    result.stored_path = std::string(AppConfig::VIDEO_INPUT_DIR) + "/" + result.stored_filename;

    std::ofstream output(result.stored_path.c_str(), std::ios::binary);
    if (!output.is_open())
    {
        error_message = "无法创建目标视频文件";
        return false;
    }

    output.write(request.file_content.data(), static_cast<std::streamsize>(request.file_content.size()));
    output.flush();

    if (!output.good())
    {
        error_message = "写入目标视频文件失败";
        return false;
    }

    result.file_size_bytes = static_cast<long long>(request.file_content.size());
    return true;
}
