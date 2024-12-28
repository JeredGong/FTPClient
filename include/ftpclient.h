// Include Guards - ftpclient.h
#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

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

// OpenSSL headers
#include <openssl/ssl.h>
#include <openssl/err.h>

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

    FTPClient();
    ~FTPClient();

    bool initSSL();
    bool upgradeToTLS();
    bool connect(const std::string& host, uint16_t port = 21);
    bool login(const std::string& username, const std::string& password);
    void disconnect();

    bool uploadFile(const std::string& localPath, 
                   const std::string& remotePath,
                   bool resume = false,
                   const ProgressCallback& progress = nullptr);

    bool downloadFile(const std::string& remotePath,
                     const std::string& localPath,
                     bool resume = false,
                     const ProgressCallback& progress = nullptr);

    void setTransferMode(TransferMode mode) { transferMode = mode; }
    bool setTransferType(TransferType type);
    std::vector<std::string> listFiles();
    std::string getCurrentDir();
    bool changeDir(const std::string& path);
    bool makeDir(const std::string& path);
    bool removeDir(const std::string& path);
    bool deleteFile(const std::string& path);

    std::string getLastError() const { return lastError; }
    std::string getSSLInfo() const;

    TLSConfig tlsConfig;

private:
    bool sendCommand(const std::string& command);
    FTPResponse getResponse();
    SOCKET createDataConnection();
    bool parsePasvResponse(const std::string& response, 
                          std::string& ip, uint16_t& port);
    int64_t getFileSize(const std::string& path);
    bool setFilePosition(int64_t pos);
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

#endif // FTP_CLIENT_H