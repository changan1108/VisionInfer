# 1. 声明要求的 CMake 最低版本
# 之所以不写得更高，是为了兼容常见 Ubuntu 开发环境，同时满足本项目当前用法。
cmake_minimum_required(VERSION 3.10)

# 2. 声明项目名称和支持的语言
project(VisionInferenceServer CXX)

# 3. 指定 C++ 标准
# 当前代码基于 C++11 编写，线程池、unordered_map、chrono 等都依赖该标准。
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 4. 将项目根目录加入头文件搜索路径
# 这样 controller/、service/、dao/、entity/ 下的头文件都可以直接按项目相对路径 include。
include_directories(${CMAKE_SOURCE_DIR})

# 5. 查找线程库和 pkg-config
# Threads::Threads 用于线程池等并发能力。
# pkg-config 用于从系统中查询 FFmpeg 的头文件目录、库目录和链接参数。
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# 6. 通过 pkg-config 查找 FFmpeg 四个核心模块
# libavformat: 打开视频文件、读取封装格式和流信息
# libavcodec : 后续做视频解码 / 编码时会依赖它
# libavutil  : FFmpeg 的基础工具库
# libswscale : 后续做图像缩放、像素格式转换时会依赖它
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

# ==========================================
# 第三方库配置
# ==========================================
# (1) jsoncpp
# 当前 Controller 层大量使用 jsoncpp 解析请求体和组装响应体。
# 这里显式加入 /usr/local/include 和 /usr/local/lib，适配本机已有安装位置。
include_directories(/usr/local/include)

link_directories(/usr/local/lib)


# (2) MySQL 客户端开发库
# 当前 DAO 层直接使用 mysqlclient 访问 vision_db。
include_directories(/usr/include)
link_directories(/usr/lib/x86_64-linux-gnu)


# (3) ONNX Runtime
# 目前还未正式接入，后续做真实推理时再补头文件和链接配置。

# 7. 收集各层源码
# GLOB_RECURSE 会递归扫描目录，新增 cpp 文件后重新 cmake 配置即可自动纳入构建。
file(GLOB_RECURSE COMMON_SOURCES "common/*.cpp")
file(GLOB_RECURSE CONTROLLER_SOURCES "controller/*.cpp")
file(GLOB_RECURSE SERVICE_SOURCES "service/*.cpp")
file(GLOB_RECURSE DAO_SOURCES "dao/*.cpp")

# 8. 生成最终可执行文件
# main.cpp 是入口，其余源码按分层结构统一编译进同一个后端程序。
add_executable(${PROJECT_NAME} 
    main.cpp 
    ${COMMON_SOURCES} 
    ${CONTROLLER_SOURCES} 
    ${SERVICE_SOURCES} 
    ${DAO_SOURCES}
)

# 9. 先链接线程库
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# 10. 链接业务依赖库
# jsoncpp      : HTTP JSON 请求 / 响应处理
# mysqlclient  : DAO 层数据库访问
# PkgConfig::FFMPEG : 由 pkg-config 聚合出的 FFmpeg 头文件和链接参数
target_link_libraries(${PROJECT_NAME} PRIVATE 
    jsoncpp 
    mysqlclient
    PkgConfig::FFMPEG
    # onnxruntime
)
