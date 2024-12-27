/**
 * @file ftpclient.h
 * @brief FTP客户端类的头文件，支持基本的FTP操作和TLS加密
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "libssl.lib")
    #pragma comment(lib, "libcrypto.lib")
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

namespace ftp {

/**
 * @brief FTP响应结构体
 */
struct FTPResponse {
    int code;           ///< 响应码
    std::string msg;    ///< 响应消息
};

/**
 * @brief 传输模式枚举
 */
enum class TransferMode {
    ACTIVE,     ///< 主动模式
    PASSIVE     ///< 被动模式
};

/**
 * @brief 传输类型枚举
 */
enum class TransferType {
    ASCII,      ///< ASCII模式
    BINARY      ///< 二进制模式
};

/**
 * @brief 传输进度回调函数类型
 */
using ProgressCallback = std::function<void(int64_t current, int64_t total)>;

/**
 * @brief SSL/TLS支持结构体
 */
struct SSLSupport {
    SSL_CTX* ctx;          ///< SSL上下文
    SSL* ssl;              ///< 控制连接SSL
    SSL* dataSSL;          ///< 数据连接SSL
    bool initialized;       ///< SSL初始化标志
    bool protected_mode;    ///< 保护模式标志

    SSLSupport() : ctx(nullptr), ssl(nullptr), dataSSL(nullptr), 
                   initialized(false), protected_mode(false) {}
};

/**
 * @brief FTP客户端类
 */
class FTPClient {
public:
    /**
     * @brief TLS配置结构体
     */
    struct TLSConfig {
        bool verify_peer;           ///< 是否验证对端证书
        std::string ca_file;        ///< CA证书文件路径
        std::string ca_path;        ///< CA证书目录路径
        std::string cert_file;      ///< 客户端证书路径
        std::string key_file;       ///< 客户端私钥路径
        
        TLSConfig() : verify_peer(true), ca_file(""), ca_path(""), 
                      cert_file(""), key_file("") {}
    };

    /**
     * @brief 构造函数
     */
    FTPClient();

    /**
     * @brief 析构函数
     */
    ~FTPClient();

    /**
     * @brief 初始化SSL
     * @return 是否成功
     */
    bool initSSL();

    /**
     * @brief 升级到TLS连接
     * @return 是否成功
     */
    bool upgradeToTLS();

    /**
     * @brief 连接到FTP服务器
     * @param host 服务器地址
     * @param port 服务器端口
     * @return 是否成功
     */
    bool connect(const std::string& host, uint16_t port = 21);

    /**
     * @brief 登录服务器
     * @param username 用户名
     * @param password 密码
     * @return 是否成功
     */
    bool login(const std::string& username, const std::string& password);

    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 上传文件
     * @param localPath 本地文件路径
     * @param remotePath 远程文件路径
     * @param resume 是否断点续传
     * @param progress 进度回调函数
     * @return 是否成功
     */
    bool uploadFile(const std::string& localPath, 
                   const std::string& remotePath,
                   bool resume = false,
                   const ProgressCallback& progress = nullptr);

    /**
     * @brief 下载文件
     * @param remotePath 远程文件路径
     * @param localPath 本地文件路径
     * @param resume 是否断点续传
     * @param progress 进度回调函数
     * @return 是否成功
     */
    bool downloadFile(const std::string& remotePath,
                     const std::string& localPath,
                     bool resume = false,
                     const ProgressCallback& progress = nullptr);

    /**
     * @brief 设置传输模式
     * @param mode 传输模式
     */
    void setTransferMode(TransferMode mode) { transferMode = mode; }

    /**
     * @brief 设置传输类型
     * @param type 传输类型
     * @return 是否成功
     */
    bool setTransferType(TransferType type);

    /**
     * @brief 获取文件列表
     * @return 文件列表
     */
    std::vector<std::string> listFiles();

    /**
     * @brief 获取当前目录
     * @return 当前目录路径
     */
    std::string getCurrentDir();

    /**
     * @brief 改变目录
     * @param path 目标目录路径
     * @return 是否成功
     */
    bool changeDir(const std::string& path);

    /**
     * @brief 创建目录
     * @param path 目录路径
     * @return 是否成功
     */
    bool makeDir(const std::string& path);

    /**
     * @brief 删除目录
     * @param path 目录路径
     * @return 是否成功
     */
    bool removeDir(const std::string& path);

    /**
     * @brief 删除文件
     * @param path 文件路径
     * @return 是否成功
     */
    bool deleteFile(const std::string& path);

    /**
     * @brief 获取最后的错误信息
     * @return 错误信息
     */
    std::string getLastError() const { return lastError; }

    /**
     * @brief TLS配置
     */
    TLSConfig tlsConfig;

    /**
     * @brief 获取SSL连接信息
     * @return SSL连接信息的字符串
     */
    std::string getSSLInfo() const;

private:
    /**
     * @brief 发送FTP命令
     * @param command 命令字符串
     * @return 是否成功
     */
    bool sendCommand(const std::string& command);

    /**
     * @brief 获取FTP响应
     * @return FTP响应结构
     */
    FTPResponse getResponse();

    /**
     * @brief 创建数据连接
     * @return 数据连接socket
     */
    SOCKET createDataConnection();

    /**
     * @brief 解析PASV响应
     * @param response PASV响应字符串
     * @param ip 解析出的IP地址
     * @param port 解析出的端口
     * @return 是否成功
     */
    bool parsePasvResponse(const std::string& response, 
                          std::string& ip, uint16_t& port);

    /**
     * @brief 获取文件大小
     * @param path 文件路径
     * @return 文件大小
     */
    int64_t getFileSize(const std::string& path);

    /**
     * @brief 设置断点续传位置
     * @param pos 续传位置
     * @return 是否成功
     */
    bool setFilePosition(int64_t pos);

    /**
     * @brief 初始化网络环境
     * @return 是否成功
     */
    static bool initNetwork();

private:
    SOCKET controlSocket;        ///< 控制连接socket
    TransferMode transferMode;   ///< 传输模式
    TransferType transferType;   ///< 传输类型
    std::string lastError;       ///< 最后的错误信息
    SSLSupport ssl;             ///< SSL/TLS支持

    static bool networkInit;     ///< 网络初始化标志
};

} // namespace ftp
