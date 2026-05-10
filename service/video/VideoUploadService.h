#ifndef VIDEO_UPLOAD_SERVICE_H
#define VIDEO_UPLOAD_SERVICE_H

#include <string>

struct VideoUploadRequest
{
    std::string submitted_by;
    std::string source_file_path;
    std::string original_filename;
    std::string file_content;
};

struct VideoUploadResult
{
    std::string original_filename;
    std::string stored_filename;
    std::string stored_path;
    long long file_size_bytes = 0;
};

class VideoUploadService
{
public:
    static bool uploadFromServerPath(const VideoUploadRequest &request, VideoUploadResult &result,
                                     std::string &error_message);
    static bool uploadFromBinary(const VideoUploadRequest &request, VideoUploadResult &result,
                                 std::string &error_message);
};

#endif // VIDEO_UPLOAD_SERVICE_H
