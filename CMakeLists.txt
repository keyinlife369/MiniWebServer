cmake_minimum_required(VERSION 3.16)
project(MiniWebServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置Boost路径
set(BOOST_ROOT "D:/CodeProject/Boost/boost-1.86.0-b2-nodocs/boost-1.86.0")
set(Boost_NO_SYSTEM_PATHS ON)
set(Boost_NO_BOOST_CMAKE ON)

# 查找Boost（只需要头文件）
find_package(Boost REQUIRED)
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_INCLUDE_DIRS}")
endif()

# Qt6设置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 编译选项 - Windows特定
if(MSVC)
    add_compile_options(/W3  /utf-8)
    add_definitions(-D_WIN32_WINNT=0x0A00)  # Windows 10+
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# 源文件
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/server.cpp
    src/http_parser.cpp
    src/logger.cpp
    src/server_monitor.cpp
)

set(HEADERS
    include/server.h
    include/thread_pool.h
    include/http_parser.h
    include/logger.h
    include/server_monitor.h
    include/mainwindow.h
)

# 可执行文件
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE 
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

# Windows特定链接
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        ws2_32    # Windows Sockets 2
        mswsock   # Microsoft Windows Sockets扩展
    )
endif()

# 复制静态文件到构建目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/static
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/static
)

# 复制Qt DLL到构建目录（Windows需要）
if(WIN32)
    # 自动部署Qt依赖
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt6
        HINTS ${QT6_INSTALL_PREFIX}/bin
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Deploying Qt dependencies..."
        )
    endif()
endif()