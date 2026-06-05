#include "service/video/VideoMetadataHelper.h"

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#ifdef __cplusplus
}
#endif

// 调用FFmpeg读取当前视频元数据，后续抽帧和推理都会建立在这一步之上(这里只读取基础信息，还没有逐帧解码)
bool VideoMetadataHelper::readMetadata(const std::string &path, VideoFileMetadata &metadata)
{
    AVFormatContext *format_context = nullptr;

    // 打开视频文件
    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }

    // 读取视频文件
    if (avformat_find_stream_info(format_context, nullptr) < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    
    if (format_context->duration != AV_NOPTS_VALUE)
    {
        metadata.duration_seconds = static_cast<double>(format_context->duration) / AV_TIME_BASE;
    }

    // 找到视频流
    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    // format_context表示整个媒体文件容器,例如一个MP4文件有视频流、音频流、字幕流，通过上面的索引找到视频流
    AVStream *video_stream = format_context->streams[video_stream_index];

    // 取出该视频流的编码参数
    AVCodecParameters *codecpar = video_stream->codecpar;

    // 提取"视频宽度和高度(分辨率)"
    metadata.width = codecpar->width;
    metadata.height = codecpar->height;

    // 提取"视频帧率"
    AVRational frame_rate = video_stream->avg_frame_rate.num != 0 ? video_stream->avg_frame_rate
                                                                  : video_stream->r_frame_rate;
    if (frame_rate.num != 0 && frame_rate.den != 0)
    {
        metadata.fps = av_q2d(frame_rate);
    }

    // 关闭视频文件
    avformat_close_input(&format_context);
    return metadata.width > 0 && metadata.height > 0;
}
