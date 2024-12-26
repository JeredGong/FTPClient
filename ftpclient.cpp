// ftpclient.cpp
#include "ftpclient.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

bool FTPClient::networkInit = false;

FTPClient::FTPClient() : 
    controlSocket(INVALID_SOCKET),
    transferMode(TransferMode::PASSIVE),
    transferType(TransferType::BINARY) {
    if (!networkInit) {
        networkInit = initNetwork();
    }
}

FTPClient::~FTPClient() {
    disconnect();
}

bool FTPClient::initNetwork() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#endif
    return true;
}

bool FTPClient::connect(const std::string& host, uint16_t port) {
    // 创建控制连接socket
    controlSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlSocket == INVALID_SOCKET) {
        lastError = "Failed to create control socket";
        return false;
    }

    // 解析主机地址
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        lastError = "Failed to resolve host address";
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
        return false;
    }

    // 连接服务器
    if (::connect(controlSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        lastError = "Failed to connect to server";
        freeaddrinfo(result);
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
        return false;
    }

    freeaddrinfo(result);

    // 获取连接响应
    FTPResponse response = getResponse();
    if (response.code != 220) {
        lastError = "Server rejected connection: " + response.msg;
        disconnect();
        return false;
    }

    return true;
}

bool FTPClient::login(const std::string& username, const std::string& password) {
    // 发送用户名
    if (!sendCommand("USER " + username)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 331 && response.code != 230) {
        lastError = "Login failed: " + response.msg;
        return false;
    }

    // 如果需要密码
    if (response.code == 331) {
        if (!sendCommand("PASS " + password)) {
            return false;
        }

        response = getResponse();
        if (response.code != 230) {
            lastError = "Login failed: " + response.msg;
            return false;
        }
    }

    return true;
}

void FTPClient::disconnect() {
    if (controlSocket != INVALID_SOCKET) {
        sendCommand("QUIT");
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
    }
}

bool FTPClient::uploadFile(const std::string& localPath, const std::string& remotePath,
                         bool resume) {
    // 打开本地文件
    std::ifstream file(localPath, std::ios::binary);
    if (!file) {
        lastError = "Cannot open local file: " + localPath;
        return false;
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    int64_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // 如果需要断点续传
    int64_t startPos = 0;
    if (resume) {
        startPos = getFileSize(remotePath);
        if (startPos > 0) {
            if (!setFilePosition(startPos)) {
                file.close();
                return false;
            }
            file.seekg(startPos);
        }
    }

    // 创建数据连接
    SOCKET dataSocket = createDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        file.close();
        return false;
    }

    // 发送STOR命令
    if (!sendCommand("STOR " + remotePath)) {
        closesocket(dataSocket);
        file.close();
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 150 && response.code != 125) {
        lastError = "Failed to initiate file transfer: " + response.msg;
        closesocket(dataSocket);
        file.close();
        return false;
    }

    // 传输文件数据
    char buffer[8192];
    int64_t transferred = startPos;
    bool success = true;

    while (!file.eof() && success) {
        file.read(buffer, sizeof(buffer));
        int readCount = static_cast<int>(file.gcount());
        
        if (readCount > 0) {
            int sent = send(dataSocket, buffer, readCount, 0);
            if (sent == SOCKET_ERROR) {
                lastError = "Failed to send file data";
                success = false;
            } else {
                transferred += sent;
            }
        }
    }

    // 关闭连接
    file.close();
    closesocket(dataSocket);

    // 获取传输完成响应
    response = getResponse();
    if (response.code != 226 && response.code != 250) {
        lastError = "File transfer failed: " + response.msg;
        return false;
    }

    return success;
}

bool FTPClient::downloadFile(const std::string& remotePath, const std::string& localPath,
                           bool resume) {
    // 获取远程文件大小
    int64_t fileSize = getFileSize(remotePath);
    if (fileSize < 0) {
        return false;
    }

    // 打开本地文件
    std::ios::openmode mode = std::ios::binary;
    int64_t startPos = 0;

    if (resume) {
        std::ifstream existingFile(localPath, std::ios::binary | std::ios::ate);
        if (existingFile) {
            startPos = existingFile.tellg();
            existingFile.close();
            if (startPos >= fileSize) {
                return true; // 文件已完全下载
            }
            mode |= std::ios::app;
        }
    }

    std::ofstream file(localPath, mode);
    if (!file) {
        lastError = "Cannot open local file: " + localPath;
        return false;
    }

    // 如果需要断点续传，设置文件位置
    if (startPos > 0) {
        if (!setFilePosition(startPos)) {
            file.close();
            return false;
        }
    }

    // 创建数据连接
    SOCKET dataSocket = createDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        file.close();
        return false;
    }

    // 发送RETR命令
    if (!sendCommand("RETR " + remotePath)) {
        closesocket(dataSocket);
        file.close();
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 150 && response.code != 125) {
        lastError = "Failed to initiate file transfer: " + response.msg;
        closesocket(dataSocket);
        file.close();
        return false;
    }

    // 接收文件数据
    char buffer[8192];
    int64_t transferred = startPos;
    bool success = true;

    while (transferred < fileSize && success) {
        int received = recv(dataSocket, buffer, sizeof(buffer), 0);
        if (received > 0) {
            file.write(buffer, received);
            transferred += received;
        } else if (received == 0) {
            break; // 连接关闭
        } else {
            lastError = "Failed to receive file data";
            success = false;
        }
    }

    // 关闭连接
    file.close();
    closesocket(dataSocket);

    // 获取传输完成响应
    response = getResponse();
    if (response.code != 226 && response.code != 250) {
        lastError = "File transfer failed: " + response.msg;
        return false;
    }

    return success;
}

bool FTPClient::setTransferType(TransferType type) {
    std::string command = "TYPE " + std::string(type == TransferType::ASCII ? "A" : "I");
    
    if (!sendCommand(command)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 200) {
        lastError = "Failed to set transfer type: " + response.msg;
        return false;
    }

    transferType = type;
    return true;
}

std::vector<std::string> FTPClient::listFiles() {
    std::vector<std::string> fileList;

    // 创建数据连接
    SOCKET dataSocket = createDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        return fileList;
    }

    // 发送LIST命令
    if (!sendCommand("LIST")) {
        closesocket(dataSocket);
        return fileList;
    }

    FTPResponse response = getResponse();
    if (response.code != 150 && response.code != 125) {
        lastError = "Failed to list directory: " + response.msg;
        closesocket(dataSocket);
        return fileList;
    }

    // 接收目录列表
    char buffer[4096];
    std::string data;

    while (true) {
        int received = recv(dataSocket, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            data += buffer;
        } else if (received == 0) {
            break;
        } else {
            lastError = "Failed to receive directory listing";
            closesocket(dataSocket);
            return fileList;
        }
    }

    closesocket(dataSocket);

    // 获取传输完成响应
    response = getResponse();
    if (response.code != 226 && response.code != 250) {
        lastError = "Directory listing failed: " + response.msg;
        return fileList;
    }

    // 解析目录列表
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            fileList.push_back(line);
        }
    }

    return fileList;
}

bool FTPClient::sendCommand(const std::string& command) {
    std::string cmd = command + "\r\n";
    if (send(controlSocket, cmd.c_str(), cmd.length(), 0) != cmd.length()) {
        lastError = "Failed to send command";
        return false;
    }
    return true;
}

FTPResponse FTPClient::getResponse() {
    FTPResponse response;
    char buffer[1024];
    std::string responseStr;

    while (true) {
        int received = recv(controlSocket, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            responseStr += buffer;

            // 检查是否收到完整响应
            size_t pos = responseStr.find_last_of('\n');
            if (pos != std::string::npos) {
                std::string lastLine = responseStr.substr(responseStr.find_last_of('\n', pos - 1) + 1);
                // 检查FTP响应格式(3个数字 + 空格)
                if (lastLine.length() >= 4 && 
                    std::isdigit(lastLine[0]) && 
                    std::isdigit(lastLine[1]) && 
                    std::isdigit(lastLine[2]) && 
                    lastLine[3] == ' ') {
                    break;
                }
            }
        } else {
            response.code = 0;
            response.msg = "Connection closed by server";
            return response;
        }
    }

    // 解析响应码和消息
    if (responseStr.length() >= 3) {
        response.code = std::stoi(responseStr.substr(0, 3));
        size_t msgStart = responseStr.find_first_of(' ');
        if (msgStart != std::string::npos) {
            response.msg = responseStr.substr(msgStart + 1);
            // 移除尾部的\r\n
            while (!response.msg.empty() && 
                   (response.msg.back() == '\n' || response.msg.back() == '\r')) {
                response.msg.pop_back();
            }
        }
    }

    return response;
}

SOCKET FTPClient::createDataConnection() {
    SOCKET dataSocket = INVALID_SOCKET;

    if (transferMode == TransferMode::PASSIVE) {
        // 被动模式
        if (!sendCommand("PASV")) {
            return INVALID_SOCKET;
        }

        FTPResponse response = getResponse();
        if (response.code != 227) {
            lastError = "Failed to enter passive mode: " + response.msg;
            return INVALID_SOCKET;
        }

        // 解析PASV响应，获取IP和端口
        std::string ip;
        uint16_t port;
        if (!parsePasvResponse(response.msg, ip, port)) {
            return INVALID_SOCKET;
        }

        // 连接到数据端口
        dataSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (dataSocket == INVALID_SOCKET) {
            lastError = "Failed to create data socket";
            return INVALID_SOCKET;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (::connect(dataSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            lastError = "Failed to connect to data port";
            closesocket(dataSocket);
            return INVALID_SOCKET;
        }
    } else {
        // 主动模式
        // 创建监听socket
        dataSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (dataSocket == INVALID_SOCKET) {
            lastError = "Failed to create data socket";
            return INVALID_SOCKET;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0; // 让系统选择可用端口

        if (bind(dataSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            lastError = "Failed to bind data socket";
            closesocket(dataSocket);
            return INVALID_SOCKET;
        }

        if (listen(dataSocket, 1) == SOCKET_ERROR) {
            lastError = "Failed to listen on data socket";
            closesocket(dataSocket);
            return INVALID_SOCKET;
        }

        // 获取分配的端口号
        socklen_t addrLen = sizeof(addr);
        if (getsockname(dataSocket, (struct sockaddr*)&addr, &addrLen) == SOCKET_ERROR) {
            lastError = "Failed to get local address";
            closesocket(dataSocket);
            return INVALID_SOCKET;
        }

        // 发送PORT命令
        struct sockaddr_in localAddr = {};
        addrLen = sizeof(localAddr);
        getsockname(controlSocket, (struct sockaddr*)&localAddr, &addrLen);

        char portCommand[64];
        unsigned char* ip = (unsigned char*)&localAddr.sin_addr;
        unsigned char p1 = (unsigned char)(ntohs(addr.sin_port) >> 8);
        unsigned char p2 = (unsigned char)(ntohs(addr.sin_port) & 0xFF);
        
        snprintf(portCommand, sizeof(portCommand), "PORT %d,%d,%d,%d,%d,%d",
                ip[0], ip[1], ip[2], ip[3], p1, p2);

        if (!sendCommand(portCommand)) {
            closesocket(dataSocket);
            return INVALID_SOCKET;
        }

        FTPResponse response = getResponse();
        if (response.code != 200) {
            lastError = "Failed to set port: " + response.msg;
            closesocket(dataSocket);
            return INVALID_SOCKET;
        }

        // 接受客户端连接
        SOCKET clientSocket = accept(dataSocket, nullptr, nullptr);
        closesocket(dataSocket);
        
        if (clientSocket == INVALID_SOCKET) {
            lastError = "Failed to accept client connection";
            return INVALID_SOCKET;
        }

        dataSocket = clientSocket;
    }

    return dataSocket;
}

bool FTPClient::parsePasvResponse(const std::string& response, std::string& ip, uint16_t& port) {
    // 查找第一个数字
    size_t start = response.find_first_of("0123456789");
    if (start == std::string::npos) {
        lastError = "Invalid PASV response format";
        return false;
    }

    // 提取IP和端口数字
    int nums[6];
    int count = 0;
    std::string num;

    for (size_t i = start; i < response.length() && count < 6; ++i) {
        if (std::isdigit(response[i])) {
            num += response[i];
        } else if (!num.empty()) {
            nums[count++] = std::stoi(num);
            num.clear();
        }
    }

    if (!num.empty() && count < 6) {
        nums[count++] = std::stoi(num);
    }

    if (count != 6) {
        lastError = "Invalid PASV response format";
        return false;
    }

    // 构造IP地址和端口
    char ipStr[32];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", nums[0], nums[1], nums[2], nums[3]);
    ip = ipStr;
    port = nums[4] * 256 + nums[5];

    return true;
}

int64_t FTPClient::getFileSize(const std::string& path) {
    if (!sendCommand("SIZE " + path)) {
        return -1;
    }

    FTPResponse response = getResponse();
    if (response.code != 213) {
        lastError = "Failed to get file size: " + response.msg;
        return -1;
    }

    return std::stoll(response.msg);
}

bool FTPClient::setFilePosition(int64_t pos) {
    if (!sendCommand("REST " + std::to_string(pos))) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 350) {
        lastError = "Failed to set file position: " + response.msg;
        return false;
    }

    return true;
}

std::string FTPClient::getCurrentDir() {
    if (!sendCommand("PWD")) {
        return "";
    }

    FTPResponse response = getResponse();
    if (response.code != 257) {
        lastError = "Failed to get current directory: " + response.msg;
        return "";
    }

    // 提取引号中的路径
    size_t start = response.msg.find('"');
    size_t end = response.msg.find('"', start + 1);
    if (start != std::string::npos && end != std::string::npos) {
        return response.msg.substr(start + 1, end - start - 1);
    }

    return response.msg;
}

bool FTPClient::changeDir(const std::string& path) {
    if (!sendCommand("CWD " + path)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 250) {
        lastError = "Failed to change directory: " + response.msg;
        return false;
    }

    return true;
}

bool FTPClient::makeDir(const std::string& path) {
    if (!sendCommand("MKD " + path)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 257) {
        lastError = "Failed to create directory: " + response.msg;
        return false;
    }

    return true;
}

bool FTPClient::removeDir(const std::string& path) {
    if (!sendCommand("RMD " + path)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 250) {
        lastError = "Failed to remove directory: " + response.msg;
        return false;
    }

    return true;
}

bool FTPClient::deleteFile(const std::string& path) {
    if (!sendCommand("DELE " + path)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 250) {
        lastError = "Failed to delete file: " + response.msg;
        return false;
    }

    return true;
}