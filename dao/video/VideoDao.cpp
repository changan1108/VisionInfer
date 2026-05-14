#include "dao/video/VideoDao.h"

#include <mysql/mysql.h>

#include "common/config/AppConfig.h"
#include "dao/db_conn/MysqlConn.h"

namespace
{
std::string escapeSql(const std::string &input)
{
    std::string escaped;
    escaped.reserve(input.size() * 2);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' || input[i] == '\'')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(input[i]);
    }
    return escaped;
}

VideoEntity buildVideoEntityFromRow(MYSQL_ROW row)
{
    VideoEntity video;
    video.id = row[0] ? std::stoi(row[0]) : 0;
    video.submitted_by = row[1] ? row[1] : "";
    video.original_filename = row[2] ? row[2] : "";
    video.stored_filename = row[3] ? row[3] : "";
    video.stored_path = row[4] ? row[4] : "";
    video.file_size_bytes = row[5] ? std::stoll(row[5]) : 0;
    video.duration = row[6] ? std::stod(row[6]) : 0.0;
    video.width = row[7] ? std::stoi(row[7]) : 0;
    video.height = row[8] ? std::stoi(row[8]) : 0;
    video.fps = row[9] ? std::stod(row[9]) : 0.0;
    video.uploaded_at = row[10] ? row[10] : "";
    return video;
}
}

bool VideoDao::insertVideo(VideoEntity &video)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "INSERT INTO videos (submitted_by, original_filename, stored_filename, stored_path, file_size_bytes, duration, width, height, fps) VALUES ('" +
                      escapeSql(video.submitted_by) + "', '" +
                      escapeSql(video.original_filename) + "', '" +
                      escapeSql(video.stored_filename) + "', '" +
                      escapeSql(video.stored_path) + "', " +
                      std::to_string(video.file_size_bytes) + ", " +
                      std::to_string(video.duration) + ", " +
                      std::to_string(video.width) + ", " +
                      std::to_string(video.height) + ", " +
                      std::to_string(video.fps) + ");";

    if (!db.update(sql))
    {
        return false;
    }

    video.id = static_cast<int>(db.getLastInsertId());
    return video.id > 0;
}

bool VideoDao::getVideoById(int video_id, VideoEntity &out_video)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "SELECT id, submitted_by, original_filename, stored_filename, stored_path, file_size_bytes, duration, width, height, fps, uploaded_at "
                      "FROM videos WHERE id = " + std::to_string(video_id) + ";";
    MYSQL_RES *res = db.query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == nullptr)
    {
        mysql_free_result(res);
        return false;
    }

    out_video = buildVideoEntityFromRow(row);
    mysql_free_result(res);
    return true;
}

bool VideoDao::getVideoByStoredPath(const std::string &stored_path, VideoEntity &out_video)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return false;
    }

    std::string sql = "SELECT id, submitted_by, original_filename, stored_filename, stored_path, file_size_bytes, duration, width, height, fps, uploaded_at "
                      "FROM videos WHERE stored_path = '" + escapeSql(stored_path) + "' LIMIT 1;";
    MYSQL_RES *res = db.query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == nullptr)
    {
        mysql_free_result(res);
        return false;
    }

    out_video = buildVideoEntityFromRow(row);
    mysql_free_result(res);
    return true;
}

std::vector<VideoEntity> VideoDao::listVideos(const VideoListFilter &filter)
{
    std::vector<VideoEntity> videos;

    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return videos;
    }

    int limit = filter.limit <= 0 ? 20 : filter.limit;
    std::string sql = "SELECT id, submitted_by, original_filename, stored_filename, stored_path, file_size_bytes, duration, width, height, fps, uploaded_at "
                      "FROM videos WHERE 1=1";

    if (!filter.submitted_by.empty())
    {
        sql += " AND submitted_by = '" + escapeSql(filter.submitted_by) + "'";
    }
    if (!filter.keyword.empty())
    {
        std::string escaped_keyword = escapeSql(filter.keyword);
        sql += " AND (original_filename LIKE '%" + escaped_keyword + "%' OR stored_filename LIKE '%" + escaped_keyword + "%')";
    }

    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit) + ";";

    MYSQL_RES *res = db.query(sql);
    if (res == nullptr)
    {
        return videos;
    }

    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        videos.push_back(buildVideoEntityFromRow(row));
    }

    mysql_free_result(res);
    return videos;
}

int VideoDao::countTasksByVideoId(int video_id)
{
    MysqlConn db;
    if (!db.connect(AppConfig::DB_USER, AppConfig::DB_PASSWORD, AppConfig::DB_NAME,
                    AppConfig::DB_HOST, AppConfig::DB_PORT))
    {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM tasks WHERE input_video_id = " + std::to_string(video_id) + ";";
    MYSQL_RES *res = db.query(sql);
    if (res == nullptr)
    {
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    int count = (row != nullptr && row[0] != nullptr) ? std::stoi(row[0]) : 0;
    mysql_free_result(res);
    return count;
}
