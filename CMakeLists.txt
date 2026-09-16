cmake_minimum_required(VERSION 3.15)

# 在 project 中显式启用 CXX 和 RC 语言（RC 是 Windows 资源编译器）
project(JMT LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 是否构建单元测试（默认开启；关闭方式：cmake -DJMT_BUILD_TESTS=OFF）
option(JMT_BUILD_TESTS "构建 JMT 单元测试与集成测试" ON)

# 业务代码（除 main.cpp 外的全部源文件），编译为静态库供 exe 与测试共用
set(JMT_CORE_SOURCES
        src/command/command_registry.cpp
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
        src/command/data_command.cpp
        src/app/app_paths.cpp
        src/app/elevation_gate.cpp
        src/app/app_runtime.cpp
        src/app/env_scope.cpp
        src/jdk/jdk_scan_service.cpp
        src/jdk/download_plan.cpp
        src/jdk/version_match.cpp
        src/jdk/java_env_service.cpp
        src/jdk/jdk_download_service.cpp
        src/jdk/jmt_path_service.cpp
        src/system/path_utils.cpp
        src/system/file_lock.cpp
        src/system/utils.cpp
        src/common/string_helper.cpp
        src/common/java_version.cpp
        src/platform/console_output.cpp
        src/platform/win_elevator.cpp
        src/platform/win_registry.cpp
        src/console/repl_engine.cpp
        src/console/repl_utils.cpp
        src/network/multi_thread_downloader.cpp
        src/network/host_throttle.cpp
)

add_library(jmt_core STATIC ${JMT_CORE_SOURCES})

# include/ 作为头文件搜索根目录
target_include_directories(jmt_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# 链接系统库（PUBLIC：测试目标链接 jmt_core 时自动继承）
target_link_libraries(jmt_core PUBLIC
        Shlwapi.lib
        Wininet.lib
        Advapi32.lib
        Winhttp.lib
)

# MSVC 专用设置
if(MSVC)
    target_compile_options(jmt_core PRIVATE /utf-8)
    set_target_properties(jmt_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# 可执行文件：main.cpp + Windows 资源
add_executable(jmt src/main.cpp)
target_link_libraries(jmt PRIVATE jmt_core)

if (WIN32)
    target_sources(jmt PRIVATE resources/app.rc)
endif()

if(MSVC)
    target_compile_options(jmt PRIVATE /utf-8)
    set_target_properties(jmt PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# 测试（tests/ 目录，使用 CTest 驱动）
# tests/ 已被 .gitignore 排除（只在本机保留），因此克隆下来的仓库可能没有该目录，
# 这里做存在性判断，避免因为缺少测试目录导致 configure 失败。
if(JMT_BUILD_TESTS)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "未找到 tests/ 目录，跳过测试构建（可用 -DJMT_BUILD_TESTS=OFF 显式关闭）")
    endif()
endif()
