#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <cstddef>

namespace AppConfig
{
constexpr int SERVER_PORT = 9527;
constexpr const char *PROJECT_ROOT = "/home/chenwanyao/graduation_project";

constexpr const char *DB_HOST = "127.0.0.1";
constexpr unsigned int DB_PORT = 3306;
constexpr const char *DB_NAME = "vision_db";
constexpr const char *DB_USER = "root";
constexpr const char *DB_PASSWORD = "123456";

constexpr const char *MODEL_STORAGE_DIR = "/home/chenwanyao/graduation_project/storage/models";
constexpr const char *VIDEO_INPUT_DIR = "/home/chenwanyao/graduation_project/storage/videos/input";
constexpr const char *VIDEO_OUTPUT_DIR = "/home/chenwanyao/graduation_project/storage/videos/output";
constexpr const char *VIDEO_FRAME_DIR = "/home/chenwanyao/graduation_project/storage/videos/frames";

constexpr std::size_t DEFAULT_THREAD_POOL_SIZE = 4;
}

#endif // APP_CONFIG_H
