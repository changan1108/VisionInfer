#include "service/video/VideoLibraryService.h"

#include "dao/video/VideoDao.h"
#include "service/video/VideoMetadataHelper.h"

bool VideoLibraryService::registerUploadedVideo(const VideoUploadRequest &request, VideoUploadResult &result,
                                                std::string &error_message)
{
    VideoEntity video;
    video.submitted_by = request.submitted_by;
    video.original_filename = result.original_filename;
    video.stored_filename = result.stored_filename;
    video.stored_path = result.stored_path;
    video.file_size_bytes = result.file_size_bytes;

    VideoFileMetadata metadata;
    if (VideoMetadataHelper::readMetadata(result.stored_path, metadata))
    {
        video.duration = metadata.duration_seconds;
        video.width = metadata.width;
        video.height = metadata.height;
        video.fps = metadata.fps;
    }

    if (!VideoDao::insertVideo(video))
    {
        error_message = "视频上传成功，但视频资源入库失败";
        return false;
    }

    result.video_id = video.id;
    result.duration = video.duration;
    result.width = video.width;
    result.height = video.height;
    result.fps = video.fps;
    return true;
}

std::vector<VideoEntity> VideoLibraryService::listVideos(const VideoListFilter &filter)
{
    return VideoDao::listVideos(filter);
}

bool VideoLibraryService::getVideoInfo(int video_id, VideoInfoView &out_info)
{
    if (!VideoDao::getVideoById(video_id, out_info.video))
    {
        return false;
    }

    out_info.task_usage_count = VideoDao::countTasksByVideoId(video_id);
    out_info.has_task_usage = out_info.task_usage_count > 0;
    return true;
}

bool VideoLibraryService::resolveVideoPathById(int video_id, std::string &out_path)
{
    VideoEntity video;
    if (!VideoDao::getVideoById(video_id, video))
    {
        return false;
    }

    out_path = video.stored_path;
    return true;
}

int VideoLibraryService::countTasksByVideoId(int video_id)
{
    return VideoDao::countTasksByVideoId(video_id);
}

int VideoLibraryService::countActiveTasksByVideoId(int video_id)
{
    return VideoDao::countActiveTasksByVideoId(video_id);
}

bool VideoLibraryService::softDeleteVideoRecord(int video_id, const std::string &deleted_by)
{
    if (video_id <= 0 || deleted_by.empty())
    {
        return false;
    }

    return VideoDao::softDeleteVideo(video_id, deleted_by);
}
