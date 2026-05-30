#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace AppConfig
{
// 使用constexpr，表示当前值是编译期常量，在编译时就计算出来/确定下来，不可修改（他与const一样都不可修改）
constexpr int SERVER_PORT = 9527;
// 这里为什么又加const，因为const属于常量指针，表示指针指向的内存上的数据不可修改，之后，constexpr用来修饰指针，表示指针指向不可修改，且编译期就确定(速度快)
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
