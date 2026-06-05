#ifndef VIDEO_METADATA_HELPER_H
#define VIDEO_METADATA_HELPER_H

#include <string>

struct VideoFileMetadata
{
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration_seconds = 0.0;
};

class VideoMetadataHelper
{
public:
    // 调用FFmpeg读取当前视频元数据，后续抽帧和推理都会建立在这一步之上(这里只读取基础信息，还没有逐帧解码)
    static bool readMetadata(const std::string &path, VideoFileMetadata &metadata);
};

#endif // VIDEO_METADATA_HELPER_H
