cmake_minimum_required(VERSION 3.17.3)
project(official_demo)

# 设置ffmpeg 依赖库以及头文件所在的目录，并存入指定变量
set(FFMPEG_LIBS_DIR /usr/local/ffmpeg/lib)
set(FFMPEG_HEADERS_DIR /usr/local/ffmpeg/include)

# 添加头文件路径到编译器的头文件搜索路径下，多个路径以空格分隔
include_directories(${FFMPEG_HEADERS_DIR})
link_directories(${FFMPEG_LIBS_DIR})


add_executable(muxing_demo muxing.c)
target_link_libraries(muxing_demo avcodec avformat avutil swscale swresample)

add_executable(metadata_demo metadata.c)
target_link_libraries(metadata_demo avformat avutil)
