cmake_minimum_required(VERSION 3.16)
project(MiniWebServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置Boost路径
if(NOT DEFINED BOOST_ROOT AND DEFINED ENV{BOOST_ROOT})
    set(BOOST_ROOT $ENV{BOOST_ROOT})
endif()
if(DEFINED BOOST_ROOT)
    set(Boost_NO_SYSTEM_PATHS ON)
    set(Boost_NO_BOOST_CMAKE ON)
    message(STATUS "Using Boost from BOOST_ROOT: ${BOOST_ROOT}")
endif()

# 查找Boost
find_package(Boost REQUIRED)
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_INCLUDE_DIRS}")
endif()

# MySQL 路径
if(NOT DEFINED MYSQL_DIR AND DEFINED ENV{MYSQL_DIR})
    set(MYSQL_DIR $ENV{MYSQL_DIR})
endif()
if(NOT MYSQL_DIR)
    # 安装路径
    set(MYSQL_DIR "D:/App/Mysql/MySQLCommunityServer9.5.0")
endif()
set(MYSQL_INCLUDE_DIR "${MYSQL_DIR}/include")
set(MYSQL_LIB_DIR "${MYSQL_DIR}/lib")
if(WIN32)
    set(MYSQL_LIB "${MYSQL_LIB_DIR}/libmysql.lib")
else()
    set(MYSQL_LIB "${MYSQL_LIB_DIR}/libmysqlclient.a")
endif()
message(STATUS "Using MySQL from: ${MYSQL_DIR}")

# OpenSSL
if(NOT DEFINED OPENSSL_ROOT_DIR AND DEFINED ENV{OPENSSL_ROOT_DIR})
    set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
endif()
if(NOT OPENSSL_ROOT_DIR)
    #  vcpkg 安装路径
    set(OPENSSL_ROOT_DIR "D:/CodeApp/vcpkg/installed/x64-windows")
endif()
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_LIB_DIR "${OPENSSL_ROOT_DIR}/lib")
set(OPENSSL_LIBS
    "${OPENSSL_LIB_DIR}/libssl.lib"
    "${OPENSSL_LIB_DIR}/libcrypto.lib"
)
message(STATUS "Using OpenSSL from: ${OPENSSL_ROOT_DIR}")

# Qt6设置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 编译选项 - Windows特定
if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_definitions(-D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    # 强制编译器把源文件和执行文件都当作 UTF-8 处理
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
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
    src/router.cpp
    src/form_parser.cpp
    src/mysql_wrapper.cpp
)

set(HEADERS
    include/server.h
    include/thread_pool.h
    include/http_parser.h
    include/logger.h
    include/server_monitor.h
    include/mainwindow.h
    include/router.h
    include/form_parser.h
    include/mysql_wrapper.h
)


add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})


# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    ${MYSQL_LIB}
    ${OPENSSL_LIBS}
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

    # 复制 MySQL DLL 到输出目录
    if(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MYSQL_LIB_DIR}/libmysql.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying libmysql.dll..."
        )
        # 复制 OpenSSL DLL
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENSSL_ROOT_DIR}/bin/libssl-3-x64.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying libssl-3-x64.dll..."
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENSSL_ROOT_DIR}/bin/libcrypto-3-x64.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying libcrypto-3-x64.dll..."
        )
        # 复制 TLS 证书
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/certs"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/certs
            COMMENT "Copying TLS certificates..."
        )
    endif()

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
# ==================== 测试框架 ====================
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    set(TEST_SOURCES
        src/http_parser.cpp
        src/form_parser.cpp
        src/router.cpp
        src/logger.cpp
        test/test_main.cpp
        test/test_http_parser.cpp
        test/test_thread_pool.cpp
        test/test_form_parser.cpp
        test/test_router.cpp
    )

    add_executable(MiniWebServer_tests ${TEST_SOURCES})
    target_include_directories(MiniWebServer_tests PRIVATE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Boost_INCLUDE_DIRS}
    )
    target_link_libraries(MiniWebServer_tests PRIVATE
        GTest::gtest_main
    )

    if(WIN32)
        target_link_libraries(MiniWebServer_tests PRIVATE ws2_32)
    endif()

    include(GoogleTest)
    gtest_discover_tests(MiniWebServer_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

# ==================== 压测工具 ====================
option(BUILD_BENCH "Build benchmark client" ON)

if(BUILD_BENCH)
    add_executable(MiniWebServer_bench
        bench/bench_client.cpp
    )
    target_include_directories(MiniWebServer_bench PRIVATE
        ${Boost_INCLUDE_DIRS}
    )
    target_link_libraries(MiniWebServer_bench PRIVATE
        ws2_32
        mswsock
    )
endif()
