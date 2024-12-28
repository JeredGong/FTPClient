/**
 * @file ftpclient.cpp
 * @brief FTP客户端类的实现文件
 */

#include "ftpclient.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <system_error>
#include <chrono>
#include <thread>

namespace ftp {

bool FTPClient::networkInit = false;

FTPClient::FTPClient() : 
    controlSocket(INVALID_SOCKET),
    transferMode(TransferMode::PASSIVE),
    transferType(TransferType::BINARY) {
    
    if (!networkInit) {
        networkInit = initNetwork();
    }
    
    // 初始化OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
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

std::string FTPClient::getSSLInfo() const {
    std::string info;
    if (ssl.ssl && ssl.initialized) {
        info += "Protocol: ";
        info += SSL_get_version(ssl.ssl);
        info += "\nCipher: ";
        info += SSL_get_cipher(ssl.ssl);
    }
    return info;
}

bool FTPClient::initSSL() {
    const SSL_METHOD* method = TLS_client_method();
    if (!method) {
        lastError = "Failed to create SSL method";
        return false;
    }

    ssl.ctx = SSL_CTX_new(method);
    if (!ssl.ctx) {
        lastError = "Failed to create SSL context";
        return false;
    }

    // 配置证书验证
    if (tlsConfig.verify_peer) {
        SSL_CTX_set_verify(ssl.ctx, SSL_VERIFY_PEER, nullptr);
        
        // 加载CA证书
        if (!tlsConfig.ca_file.empty() || !tlsConfig.ca_path.empty()) {
            if (!SSL_CTX_load_verify_locations(ssl.ctx, 
                tlsConfig.ca_file.empty() ? nullptr : tlsConfig.ca_file.c_str(),
                tlsConfig.ca_path.empty() ? nullptr : tlsConfig.ca_path.c_str())) {
                lastError = "Failed to load CA certificates";
                return false;
            }
        } else {
            SSL_CTX_set_default_verify_paths(ssl.ctx);
        }
    }

    // 加载客户端证书
    if (!tlsConfig.cert_file.empty()) {
        if (SSL_CTX_use_certificate_file(ssl.ctx, tlsConfig.cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            lastError = "Failed to load client certificate";
            return false;
        }
    }

    // 加载客户端私钥
    if (!tlsConfig.key_file.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ssl.ctx, tlsConfig.key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            lastError = "Failed to load client private key";
            return false;
        }

        // 验证私钥
        if (!SSL_CTX_check_private_key(ssl.ctx)) {
            lastError = "Client private key does not match the certificate public key";
            return false;
        }
    }

    return true;
}

bool FTPClient::upgradeToTLS() {
    if (!sendCommand("AUTH TLS")) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 234) {
        lastError = "Server doesn't support TLS: " + response.msg;
        return false;
    }

    ssl.ssl = SSL_new(ssl.ctx);
    if (!ssl.ssl) {
        lastError = "Failed to create SSL object";
        return false;
    }

    SSL_set_fd(ssl.ssl, static_cast<int>(controlSocket));

    if (SSL_connect(ssl.ssl) != 1) {
        lastError = "SSL handshake failed";
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        lastError += ": " + std::string(err_buf);
        return false;
    }

    ssl.initialized = true;

    // 设置保护缓冲区大小为0
    if (!sendCommand("PBSZ 0")) {
        return false;
    }
    response = getResponse();
    if (response.code != 200) {
        lastError = "Failed to set protection buffer size: " + response.msg;
        return false;
    }

    // 设置数据通道保护级别为私密
    if (!sendCommand("PROT P")) {
        return false;
    }
    response = getResponse();
    if (response.code != 200) {
        lastError = "Failed to set protection level: " + response.msg;
        return false;
    }

    ssl.protected_mode = true;
    return true;
}

bool FTPClient::connect(const std::string& host, uint16_t port) {
    controlSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlSocket == INVALID_SOCKET) {
        lastError = "Failed to create control socket";
        return false;
    }

    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        lastError = "Failed to resolve host address";
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
        return false;
    }

    if (::connect(controlSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        lastError = "Failed to connect to server";
        freeaddrinfo(result);
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
        return false;
    }

    freeaddrinfo(result);

    FTPResponse response = getResponse();
    if (response.code != 220) {
        lastError = "Server rejected connection: " + response.msg;
        disconnect();
        return false;
    }

    return true;
}

bool FTPClient::sendCommand(const std::string& command) {
    std::string cmd = command + "\r\n";
    
    if (ssl.initialized) {
        int sent = SSL_write(ssl.ssl, cmd.c_str(), static_cast<int>(cmd.length()));
        if (sent <= 0) {
            int err = SSL_get_error(ssl.ssl, sent);
            char err_buf[256];
            ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
            lastError = "Failed to send command over SSL: " + std::string(err_buf);
            return false;
        }
    } else {
        if (send(controlSocket, cmd.c_str(), cmd.length(), 0) != cmd.length()) {
            lastError = "Failed to send command";
            return false;
        }
    }
    return true;
}

FTPResponse FTPClient::getResponse() {
    FTPResponse response;
    char buffer[1024];
    std::string responseStr;

    while (true) {
        int received;
        if (ssl.initialized) {
            received = SSL_read(ssl.ssl, buffer, sizeof(buffer) - 1);
            if (received <= 0) {
                int err = SSL_get_error(ssl.ssl, received);
                char err_buf[256];
                ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
                response.code = 0;
                response.msg = "SSL read error: " + std::string(err_buf);
                return response;
            }
        } else {
            received = recv(controlSocket, buffer, sizeof(buffer) - 1, 0);
            if (received <= 0) {
                response.code = 0;
                response.msg = "Connection closed by server";
                return response;
            }
        }

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

bool FTPClient::login(const std::string& username, const std::string& password) {
    if (!sendCommand("USER " + username)) {
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 331 && response.code != 230) {
        lastError = "Login failed: " + response.msg;
        return false;
    }

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
        if (ssl.initialized) {
            sendCommand("QUIT");
            if (ssl.ssl) {
                SSL_shutdown(ssl.ssl);
                SSL_free(ssl.ssl);
                ssl.ssl = nullptr;
            }
            if (ssl.ctx) {
                SSL_CTX_free(ssl.ctx);
                ssl.ctx = nullptr;
            }
            ssl.initialized = false;
            ssl.protected_mode = false;
        } else {
            sendCommand("QUIT");
        }
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
    }
}

SOCKET FTPClient::createDataConnection() {
    SOCKET dataSocket = INVALID_SOCKET;

    if (transferMode == TransferMode::PASSIVE) {
        if (!sendCommand("PASV")) {
            return INVALID_SOCKET;
        }

        FTPResponse response = getResponse();
        if (response.code != 227) {
            lastError = "Failed to enter passive mode: " + response.msg;
            return INVALID_SOCKET;
        }

        std::string ip;
        uint16_t port;
        if (!parsePasvResponse(response.msg, ip, port)) {
            return INVALID_SOCKET;
        }

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

        // 如果是TLS模式，为数据连接建立SSL
        if (ssl.protected_mode) {
            ssl.dataSSL = SSL_new(ssl.ctx);
            if (!ssl.dataSSL) {
                lastError = "Failed to create SSL object for data connection";
                closesocket(dataSocket);
                return INVALID_SOCKET;
            }

            // 添加会话恢复
            SSL_set_fd(ssl.dataSSL, static_cast<int>(dataSocket));
            SSL_set_session(ssl.dataSSL, SSL_get_session(ssl.ssl));

            if (SSL_connect(ssl.dataSSL) != 1) {
                lastError = "SSL handshake failed for data connection";
                SSL_free(ssl.dataSSL);
                ssl.dataSSL = nullptr;
                closesocket(dataSocket);
                return INVALID_SOCKET;
            }
        }
    } else {
        // Active mode implementation...
        // [完善主动模式的代码]
    }

    return dataSocket;
}
bool FTPClient::uploadFile(const std::string& localPath, 
                         const std::string& remotePath,
                         bool resume,
                         const ProgressCallback& progress) {
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

    // 处理断点续传
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
        if (ssl.dataSSL) {
            SSL_free(ssl.dataSSL);
            ssl.dataSSL = nullptr;
        }
        closesocket(dataSocket);
        file.close();
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 150 && response.code != 125) {
        lastError = "Failed to initiate file transfer: " + response.msg;
        if (ssl.dataSSL) {
            SSL_free(ssl.dataSSL);
            ssl.dataSSL = nullptr;
        }
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
            int sent;
            if (ssl.dataSSL) {
                sent = SSL_write(ssl.dataSSL, buffer, readCount);
            } else {
                sent = send(dataSocket, buffer, readCount, 0);
            }

            if (sent <= 0) {
                lastError = "Failed to send file data";
                success = false;
            } else {
                transferred += sent;
                if (progress) {
                    progress(transferred, fileSize);
                }
            }
        }
    }

    // 关闭连接
    file.close();
    if (ssl.dataSSL) {
        SSL_shutdown(ssl.dataSSL);
        SSL_free(ssl.dataSSL);
        ssl.dataSSL = nullptr;
    }
    closesocket(dataSocket);

    // 获取传输完成响应
    response = getResponse();
    if (response.code != 226 && response.code != 250) {
        lastError = "File transfer failed: " + response.msg;
        return false;
    }

    return success;
}

bool FTPClient::downloadFile(const std::string& remotePath,
                           const std::string& localPath,
                           bool resume,
                           const ProgressCallback& progress) {
    // 获取远程文件大小
    int64_t fileSize = getFileSize(remotePath);
    if (fileSize < 0) {
        return false;
    }

    // 处理断点续传
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

    // 打开本地文件
    std::ofstream file(localPath, mode);
    if (!file) {
        lastError = "Cannot open local file: " + localPath;
        return false;
    }

    // 设置断点续传位置
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
        if (ssl.dataSSL) {
            SSL_free(ssl.dataSSL);
            ssl.dataSSL = nullptr;
        }
        closesocket(dataSocket);
        file.close();
        return false;
    }

    FTPResponse response = getResponse();
    if (response.code != 150 && response.code != 125) {
        lastError = "Failed to initiate file transfer: " + response.msg;
        if (ssl.dataSSL) {
            SSL_free(ssl.dataSSL);
            ssl.dataSSL = nullptr;
        }
        closesocket(dataSocket);
        file.close();
        return false;
    }

    // 接收文件数据
    char buffer[8192];
    int64_t transferred = startPos;
    bool success = true;

    while (transferred < fileSize && success) {
        int received;
        if (ssl.dataSSL) {
            received = SSL_read(ssl.dataSSL, buffer, sizeof(buffer));
        } else {
            received = recv(dataSocket, buffer, sizeof(buffer), 0);
        }

        if (received > 0) {
            file.write(buffer, received);
            transferred += received;
            if (progress) {
                progress(transferred, fileSize);
            }
        } else if (received == 0) {
            break; // 连接关闭
        } else {
            lastError = "Failed to receive file data";
            success = false;
        }
    }

    // 关闭连接
    file.close();
    if (ssl.dataSSL) {
        SSL_shutdown(ssl.dataSSL);
        SSL_free(ssl.dataSSL);
        ssl.dataSSL = nullptr;
    }
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
    const char* typeStr = (type == TransferType::ASCII) ? "A" : "I";
    if (!sendCommand("TYPE " + std::string(typeStr))) {
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

    SOCKET dataSocket = createDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        return fileList;
    }

    if (!sendCommand("LIST")) {
        if (ssl.dataSSL) {
            SSL_free(ssl.dataSSL);
            ssl.dataSSL = nullptr;
        }
        closesocket(dataSocket);
        return fileList;
    }

    FTPResponse response = getResponse();
    if (response.code != 150 && response.code != 125) {
        lastError = "Failed to list directory: " + response.msg;
        if (ssl.dataSSL) {
            SSL_free(ssl.dataSSL);
            ssl.dataSSL = nullptr;
        }
        closesocket(dataSocket);
        return fileList;
    }

    char buffer[4096];
    std::string data;

    while (true) {
        int received;
        if (ssl.dataSSL) {
            received = SSL_read(ssl.dataSSL, buffer, sizeof(buffer) - 1);
        } else {
            received = recv(dataSocket, buffer, sizeof(buffer) - 1, 0);
        }

        if (received > 0) {
            buffer[received] = '\0';
            data += buffer;
        } else if (received == 0) {
            break;
        } else {
            lastError = "Failed to receive directory listing";
            if (ssl.dataSSL) {
                SSL_free(ssl.dataSSL);
                ssl.dataSSL = nullptr;
            }
            closesocket(dataSocket);
            return fileList;
        }
    }

    if (ssl.dataSSL) {
        SSL_shutdown(ssl.dataSSL);
        SSL_free(ssl.dataSSL);
        ssl.dataSSL = nullptr;
    }
    closesocket(dataSocket);

    response = getResponse();
    if (response.code != 226 && response.code != 250) {
        lastError = "Directory listing failed: " + response.msg;
        return fileList;
    }

    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            fileList.push_back(line);
        }
    }

    return fileList;
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

bool FTPClient::parsePasvResponse(const std::string& response, std::string& ip, uint16_t& port) {
    size_t start = response.find_first_of("0123456789");
    if (start == std::string::npos) {
        lastError = "Invalid PASV response format";
        return false;
    }

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

    try {
        return std::stoll(response.msg);
    } catch (const std::exception& e) {
        lastError = "Invalid file size format: " + std::string(e.what());
        return -1;
    }
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

} // namespace ftp