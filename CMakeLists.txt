# 1. 声明要求的 CMake 最低版本
# 之所以不写得更高，是为了兼容常见 Ubuntu 开发环境，同时满足本项目当前用法。
cmake_minimum_required(VERSION 3.10)

# 2. 声明项目名称和支持的语言
project(VisionInferenceServer CXX)

# 3. 指定 C++ 标准
# 目前项目原有代码用到的特性在 C++11 就能工作，但接入 ONNX Runtime 1.25.x 后，
# 其官方 C++ 头文件已经依赖更高标准（至少需要 C++17）。
# 因此这里统一升级到 C++17，避免推理层接入后出现头文件编译错误。
set(CMAKE_CXX_STANDARD 17)
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
# 这里使用官方 Linux x64 预编译 SDK，统一安装在 /home/chenwanyao/onnxruntime。
# include 目录提供 onnxruntime_cxx_api.h 等头文件；
# lib 目录提供 libonnxruntime.so，后续 YoloInference 会用它做真实模型加载。
set(ONNXRUNTIME_ROOT "/home/chenwanyao/onnxruntime")
include_directories(${ONNXRUNTIME_ROOT}/include)
link_directories(${ONNXRUNTIME_ROOT}/lib)

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
# onnxruntime  : 模型加载、会话创建和后续真实推理
target_link_libraries(${PROJECT_NAME} PRIVATE 
    jsoncpp 
    mysqlclient
    PkgConfig::FFMPEG
    onnxruntime
)

# 11. 运行时库搜索路径
# 由于 onnxruntime 安装在自定义目录下，不在系统默认动态库搜索路径里，
# 这里把其 lib 目录写入可执行文件的 rpath，避免运行时再手动导出 LD_LIBRARY_PATH。
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
    INSTALL_RPATH "${ONNXRUNTIME_ROOT}/lib"
)
