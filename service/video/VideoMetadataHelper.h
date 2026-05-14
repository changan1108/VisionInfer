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
    static bool readMetadata(const std::string &path, VideoFileMetadata &metadata);
};

#endif // VIDEO_METADATA_HELPER_H
