#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <cstddef>
#include <cstdint>

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

constexpr std::size_t TASK_DISPATCH_POOL_SIZE = 2;
constexpr std::size_t TASK_DISPATCH_QUEUE_CAPACITY = 128;
constexpr std::size_t VIDEO_PROCESS_POOL_SIZE = 4;
constexpr std::size_t VIDEO_PROCESS_QUEUE_CAPACITY = 32;
constexpr std::size_t DB_POOL_MIN_SIZE = 4;
constexpr std::size_t DB_POOL_MAX_SIZE = 16;
constexpr std::uint64_t MIN_FREE_DISK_BYTES_FOR_TASK = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr int ONNX_INTRA_OP_THREADS = 1;
constexpr int ONNX_INTER_OP_THREADS = 1;
}

#endif // APP_CONFIG_H
