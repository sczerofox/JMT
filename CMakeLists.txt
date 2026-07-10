cmake_minimum_required(VERSION 3.15)

# 在 project 中显式启用 CXX 和 RC 语言（RC 是 Windows 资源编译器）
project(JMT LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件列表（包含 app.rc）
set(SOURCES
        src/main.cpp
        src/core/command_registry.cpp
        src/command/search_command.cpp
        src/command/list_command.cpp
        src/command/use_command.cpp
        src/command/env_command.cpp
        src/command/remove_command.cpp
        src/command/download_command.cpp
        src/command/version_command.cpp
        src/command/shell_command.cpp
        src/command/help_command.cpp
        src/command/rollback_command.cpp
        src/command/clean_trash_command.cpp
        src/repl/repl_engine.cpp
        src/repl/repl_utils.cpp
        src/service/java_env_service.cpp
        src/service/jmt_path_service.cpp
        src/service/jdk_scan_service.cpp
        src/service/jdk_download_service.cpp
        src/infrastructure/registry_operator.cpp
        src/infrastructure/path_utils.cpp
        src/infrastructure/file_lock.cpp
        src/infrastructure/elevation_helper.cpp
        src/print/color_print.cpp
        src/print/console_progress.cpp
        src/utils/utils.cpp
        src/utils/string_helper.cpp
        src/network/multi_thread_downloader.cpp   # 新增这一行
)

# 仅在 Windows 下添加资源文件（其他平台不添加）
if (WIN32)
    list(APPEND SOURCES src/app.rc)
endif()

add_executable(jmt ${SOURCES})

target_include_directories(jmt PRIVATE src)

# 链接系统库
target_link_libraries(jmt
        Shlwapi.lib
        Wininet.lib
        Advapi32.lib
        Winhttp.lib
)

# MSVC 专用设置
if(MSVC)
    target_compile_options(jmt PRIVATE /utf-8)
    set_target_properties(jmt PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()