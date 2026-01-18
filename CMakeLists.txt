cmake_minimum_required(VERSION 3.10)
project(finance_manager C)

# CMakeLists.txt
# 启用调试模式（跳过登录）
add_compile_definitions(DEBUG_MODE)

set(CMAKE_C_STANDARD 99)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# 源文件列表（仅核心）
set(SOURCES
    main.c
    auth.c
    utils.c
    finance.c
    sha256.c
    sqlite3.c
    settings.c
)

add_executable(finance_manager ${SOURCES})

# Windows 控制台程序（避免弹出黑窗问题）
if(WIN32)
    target_link_options(finance_manager PRIVATE -mconsole)
endif()