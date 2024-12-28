# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(FTPClient)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSYS2环境下的特殊设置
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32_WINNT=0x0601")
    
    # 设置OpenSSL路径
    set(OPENSSL_ROOT_DIR "D:/MSYS2/mingw64")
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(_OPENSSL_ROOT_HINTS ${OPENSSL_ROOT_DIR})
    set(_OPENSSL_ROOT_PATHS ${OPENSSL_ROOT_DIR})
    set(_OPENSSL_ROOT_HINTS_AND_PATHS
        HINTS ${_OPENSSL_ROOT_HINTS}
        PATHS ${_OPENSSL_ROOT_PATHS}
    )
    
    # 手动设置OpenSSL库路径
    find_library(OPENSSL_SSL_LIBRARY
        NAMES ssl ssleay32 ssleay32MD libssl
        HINTS ${OPENSSL_ROOT_DIR}/lib
    )
    
    find_library(OPENSSL_CRYPTO_LIBRARY
        NAMES crypto libeay32 libeay32MD libcrypto
        HINTS ${OPENSSL_ROOT_DIR}/lib
    )
    
    # 设置OpenSSL包含目录
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
endif()

# 查找必要的包
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# 查找 JsonCpp
find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATHS "D:/MSYS2/mingw64/include")
find_library(JSONCPP_LIBRARY NAMES jsoncpp libjsoncpp PATHS "D:/MSYS2/mingw64/lib")

message(STATUS "JsonCpp include dir: ${JSONCPP_INCLUDE_DIR}")
message(STATUS "JsonCpp library: ${JSONCPP_LIBRARY}")

# 输出调试信息
message(STATUS "OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")

# 添加外部依赖目录
add_subdirectory(external)

# 包含目录
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/websocketpp
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIR}
)

# 源文件
set(SOURCES
    src/ftpclient.cpp
    src/ftpwebsocket.cpp
    src/main.cpp
)

# 创建可执行文件
add_executable(ftpclient ${SOURCES})

# 链接库
target_link_libraries(ftpclient
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIBRARY}
    ws2_32
    wsock32
)

# Windows特定设置
if(WIN32)
    target_link_libraries(ftpclient crypt32)
endif()

# 安装规则
install(TARGETS ftpclient DESTINATION bin)

# 复制依赖DLL到构建目录
if(WIN32)
    add_custom_command(TARGET ftpclient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${OPENSSL_ROOT_DIR}/bin/libssl-3-x64.dll
            ${OPENSSL_ROOT_DIR}/bin/libcrypto-3-x64.dll
            $<TARGET_FILE_DIR:ftpclient>
    )
endif()