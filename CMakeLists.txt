# 1. 声明要求的 cmake 最低版本
cmake_minimum_required(VERSION 3.10)

# 2. 声明项目名称和支持的语言
project(VisionInferenceServer CXX)

# 3. 指定 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 4. 指定头文件搜索路径：直接指向项目根目录
# 这样在代码中就可以使用绝对基于项目根目录的路径，如 #include "core/network/Router.h"
include_directories(${CMAKE_SOURCE_DIR})

# 5. 查找系统必带的多线程库
find_package(Threads REQUIRED)

# ==========================================
# 待引入的第三方库
# ==========================================
# (1)jsoncpp
# 确保编译器能找到头文件 (你在 /usr/local/include 放入了 json/ 文件夹)
include_directories(/usr/local/include)

# 确保编译器能找到库文件 (你把 libjsoncpp.a 放在了 /usr/local/lib)
# 所以我们也需要把 lib 目录当作库搜索路径
link_directories(/usr/local/lib)


# (2)mysql-devel
include_directories(/usr/include)
link_directories(/usr/lib/x86_64-linux-gnu)



# (3)avcodec avformat avutil swscale 
# (4)onnxruntime

# 6. 收集所有业务层目录下的 .cpp 源文件
# GLOB_RECURSE 会自动向下遍历搜寻这些文件夹下的所有 cpp 文件
file(GLOB_RECURSE COMMON_SOURCES "common/*.cpp")
file(GLOB_RECURSE CONTROLLER_SOURCES "controller/*.cpp")
file(GLOB_RECURSE SERVICE_SOURCES "service/*.cpp")
file(GLOB_RECURSE DAO_SOURCES "dao/*.cpp")

# 7. 生成可执行文件 (包括 main.cpp 和上面收集的所有源文件)
add_executable(${PROJECT_NAME} 
    main.cpp 
    ${COMMON_SOURCES} 
    ${CONTROLLER_SOURCES} 
    ${SERVICE_SOURCES} 
    ${DAO_SOURCES}
)

# 8. 链接动态库
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    jsoncpp 
    mysqlclient
    # avcodec avformat avutil swscale 
    # onnxruntime
)

