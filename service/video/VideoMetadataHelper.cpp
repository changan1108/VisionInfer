#include "service/video/VideoMetadataHelper.h"

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#ifdef __cplusplus
}
#endif

bool VideoMetadataHelper::readMetadata(const std::string &path, VideoFileMetadata &metadata)
{
    AVFormatContext *format_context = nullptr;
    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    if (format_context->duration != AV_NOPTS_VALUE)
    {
        metadata.duration_seconds = static_cast<double>(format_context->duration) / AV_TIME_BASE;
    }

    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    AVStream *video_stream = format_context->streams[video_stream_index];
    AVCodecParameters *codecpar = video_stream->codecpar;
    metadata.width = codecpar->width;
    metadata.height = codecpar->height;

    AVRational frame_rate = video_stream->avg_frame_rate.num != 0 ? video_stream->avg_frame_rate
                                                                  : video_stream->r_frame_rate;
    if (frame_rate.num != 0 && frame_rate.den != 0)
    {
        metadata.fps = av_q2d(frame_rate);
    }

    avformat_close_input(&format_context);
    return metadata.width > 0 && metadata.height > 0;
}
