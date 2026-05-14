#ifndef VIDEO_ENTITY_H
#define VIDEO_ENTITY_H

#include <string>

struct VideoEntity
{
    int id = 0;
    std::string submitted_by;
    std::string original_filename;
    std::string stored_filename;
    std::string stored_path;
    long long file_size_bytes = 0;
    double duration = 0.0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::string uploaded_at;
};

struct VideoListFilter
{
    int limit = 20;
    std::string submitted_by;
    std::string keyword;
};

#endif // VIDEO_ENTITY_H
