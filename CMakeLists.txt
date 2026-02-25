cmake_minimum_required(VERSION 3.15)
project(webserver)


# 源文件列表
file(GLOB_RECURSE SRC ${PROJECT_SOURCE_DIR}/src/*.cpp)

# 兜底：确保 Config 实现被编进目标（避免 GLOB/CMake 缓存导致漏编进而出现 undefined reference）
list(APPEND SRC ${PROJECT_SOURCE_DIR}/src/utils/config.cpp)

# 设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)



# include目录
include_directories(${PROJECT_SOURCE_DIR}/include)



# 添加可执行文件
add_executable(webserver ${SRC}) 

# 链接 pthread 库（线程池需要）
find_package(Threads REQUIRED)
target_link_libraries(webserver Threads::Threads)

# 寻找 MySQL client (libmysqlclient) 
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MYSQLCLIENT QUIET mysqlclient)
endif()

if(MYSQLCLIENT_FOUND)
    message(STATUS "Found mysqlclient via pkg-config: ${MYSQLCLIENT_INCLUDEDIR}")
    include_directories(${MYSQLCLIENT_INCLUDEDIR})
    target_link_libraries(webserver ${MYSQLCLIENT_LIBRARIES})
else()
    find_library(MYSQLCLIENT_LIB NAMES mysqlclient libmysqlclient)
    find_path(MYSQLCLIENT_INCLUDE NAMES mysql.h)
    if(MYSQLCLIENT_LIB AND MYSQLCLIENT_INCLUDE)
        message(STATUS "Found mysqlclient: ${MYSQLCLIENT_LIB}")
        include_directories(${MYSQLCLIENT_INCLUDE})
        target_link_libraries(webserver ${MYSQLCLIENT_LIB})
    else()
        message(WARNING "mysqlclient library not found. Install libmysqlclient-dev (Debian/Ubuntu) or mysql-devel (RHEL) and re-run CMake if you need MySQL support.")
    endif()
endif()

# 复制 html 目录到 bin
add_custom_command(TARGET webserver POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${PROJECT_SOURCE_DIR}/src/html
                   ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/html)