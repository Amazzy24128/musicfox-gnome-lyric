cmake_minimum_required(VERSION 3.16)
project(musicfox-lyrics-backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必需的包
find_package(PkgConfig REQUIRED)

# 查找 GLib 和 GIO (用于 D-Bus)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)

# 查找 JSON 库
find_package(nlohmann_json REQUIRED)

# 包含目录
include_directories(include)
include_directories(${GLIB_INCLUDE_DIRS})
include_directories(${GIO_INCLUDE_DIRS})

# 源文件
set(SOURCES
    src/main.cpp
    src/lyric_fetcher.cpp
    src/dbus_client.cpp
)

# 创建可执行文件
add_executable(musicfox-lyrics-backend ${SOURCES})

# 链接库
target_link_libraries(musicfox-lyrics-backend 
    ${GLIB_LIBRARIES}
    ${GIO_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# 编译标志
target_compile_options(musicfox-lyrics-backend PRIVATE 
    ${GLIB_CFLAGS_OTHER}
    ${GIO_CFLAGS_OTHER}
)

# 安装规则
install(TARGETS musicfox-lyrics-backend 
    RUNTIME DESTINATION bin
)
