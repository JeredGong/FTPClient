/**
 * @file main.cpp
 * @brief 程序入口文件
 */

#include "ftpwebsocket.h"
#include <iostream>
#include <csignal>

ftp::FTPWebSocketServer* server = nullptr;

void signalHandler(int signum) {
    if (server) {
        std::cout << "\nStopping server..." << std::endl;
        server->stop();
    }
    exit(signum);
}

int main() {
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        // 创建并启动WebSocket服务器
        server = new ftp::FTPWebSocketServer(9002);
        std::cout << "WebSocket server starting on port 9002..." << std::endl;
        std::cout << "Press Ctrl+C to stop the server." << std::endl;
        server->run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    delete server;
    return 0;
}