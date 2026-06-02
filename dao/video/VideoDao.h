#ifndef VIDEO_DAO_H
#define VIDEO_DAO_H

#include <vector>

#include "entity/VideoEntity.h"

class VideoDao
{
public:
    static bool insertVideo(VideoEntity &video);
    static bool getVideoById(int video_id, VideoEntity &out_video);
    static bool getVideoByStoredPath(const std::string &stored_path, VideoEntity &out_video);
    static std::vector<VideoEntity> listVideos(const VideoListFilter &filter);
    static int countTasksByVideoId(int video_id);
    static int countActiveTasksByVideoId(int video_id);
    static bool softDeleteVideo(int video_id, const std::string &deleted_by);
};

#endif // VIDEO_DAO_H
