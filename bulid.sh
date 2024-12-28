#!/bin/bash

# 确保在MSYS2环境中运行
if [ ! -f "/msys2.exe" ]; then
    echo "This script must be run in MSYS2 environment!"
    exit 1
fi

# 安装依赖
echo "Installing dependencies..."
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-boost \
    mingw-w64-x86_64-jsoncpp \
    mingw-w64-x86_64-asio \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-make \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-openssl \
    mingw-w64-x86_64-ca-certificates \
    git

# 下载WebSocket++
if [ ! -d "external/websocketpp" ]; then
    echo "Downloading WebSocket++..."
    mkdir -p external
    cd external
    git clone https://github.com/zaphoyd/websocketpp.git
    cd ..
fi

# 创建并进入构建目录
echo "Creating build directory..."
rm -rf build
mkdir -p build
cd build

# 生成构建文件
echo "Generating build files..."
cmake -G "MSYS Makefiles" ..

# 编译
echo "Building project..."
make -j4

if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
    echo "The executable is located at: build/ftpclient.exe"
    
    # 复制必要的DLL文件到可执行文件目录
    echo "Copying required DLLs..."
    
    # 复制boost系统DLL
    for f in /mingw64/bin/libboost_system*.dll; do
        if [ -f "$f" ]; then
            cp "$f" ./
        fi
    done
    
    # 复制jsoncpp DLL
    for f in /mingw64/bin/libjsoncpp*.dll; do
        if [ -f "$f" ]; then
            cp "$f" ./
        fi
    done
    
    # 复制其他必需DLL
    cp /mingw64/bin/libssl-3-x64.dll ./
    cp /mingw64/bin/libcrypto-3-x64.dll ./
    cp /mingw64/bin/libstdc++-6.dll ./
    cp /mingw64/bin/libgcc_s_seh-1.dll ./
    cp /mingw64/bin/libwinpthread-1.dll ./
    cp /mingw64/bin/zlib1.dll ./
    
    echo "Done! You can now run ftpclient.exe"
else
    echo "Build failed!"
    exit 1
fi