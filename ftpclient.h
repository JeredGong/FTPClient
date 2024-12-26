#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

// FTP响应结构体
struct FTPResponse {
    int code;           // 响应码
    std::string msg;    // 响应消息
};

// 传输模式枚举
enum class TransferMode {
    ACTIVE,     // 主动模式
    PASSIVE     // 被动模式
};

// 传输类型枚举
enum class TransferType {
    ASCII,      // ASCII模式
    BINARY      // 二进制模式
};

class FTPClient {
public:
    FTPClient();
    ~FTPClient();

    // 连接服务器
    bool connect(const std::string& host, uint16_t port = 21);
    // 登录
    bool login(const std::string& username, const std::string& password);
    // 断开连接
    void disconnect();

    // 上传文件
    bool uploadFile(const std::string& localPath, const std::string& remotePath, 
                   bool resume = false);
    // 下载文件
    bool downloadFile(const std::string& remotePath, const std::string& localPath, 
                     bool resume = false);
    
    // 设置传输模式
    void setTransferMode(TransferMode mode) { transferMode = mode; }
    // 设置传输类型
    bool setTransferType(TransferType type);

    // 获取文件列表
    std::vector<std::string> listFiles();
    // 获取当前目录
    std::string getCurrentDir();
    // 改变目录
    bool changeDir(const std::string& path);
    // 创建目录
    bool makeDir(const std::string& path);
    // 删除目录
    bool removeDir(const std::string& path);
    // 删除文件
    bool deleteFile(const std::string& path);

    // 获取错误信息
    std::string getLastError() const { return lastError; }

private:
    // 发送FTP命令
    bool sendCommand(const std::string& command);
    // 获取FTP响应
    FTPResponse getResponse();
    
    // 创建数据连接
    SOCKET createDataConnection();
    // 解析PASV响应
    bool parsePasvResponse(const std::string& response, std::string& ip, uint16_t& port);
    
    // 获取文件大小
    int64_t getFileSize(const std::string& path);
    // 设置断点续传位置
    bool setFilePosition(int64_t pos);

    SOCKET controlSocket;        // 控制连接socket
    TransferMode transferMode;   // 传输模式
    TransferType transferType;   // 传输类型
    std::string lastError;       // 最后的错误信息

    // 网络初始化标志
    static bool networkInit;
    // 网络环境初始化
    static bool initNetwork();
};