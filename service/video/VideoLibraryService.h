#ifndef VIDEO_LIBRARY_SERVICE_H
#define VIDEO_LIBRARY_SERVICE_H

#include <vector>

#include "entity/VideoEntity.h"
#include "service/video/VideoUploadService.h"

struct VideoInfoView
{
    VideoEntity video;
    int task_usage_count = 0;
    bool has_task_usage = false;
};

class VideoLibraryService
{
public:
    static bool registerUploadedVideo(const VideoUploadRequest &request, VideoUploadResult &result,
                                      std::string &error_message);
    static std::vector<VideoEntity> listVideos(const VideoListFilter &filter);
    static bool getVideoInfo(int video_id, VideoInfoView &out_info);
    static bool resolveVideoPathById(int video_id, std::string &out_path);
    static int countTasksByVideoId(int video_id);
    static int countActiveTasksByVideoId(int video_id);
    static bool softDeleteVideoRecord(int video_id, const std::string &deleted_by);
};

#endif // VIDEO_LIBRARY_SERVICE_H
