// Include Guards - ftpwebsocket.h
#ifndef FTP_WEBSOCKET_H
#define FTP_WEBSOCKET_H

#define ASIO_STANDALONE
#define _WEBSOCKETPP_CPP11_INTERNAL_

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <json/json.h>
#include <memory>
#include <map>
#include <functional>
#include <mutex>

namespace ftp {

// Forward declarations
class FTPClient;
struct FTPResponse;
enum class TransferMode;
enum class TransferType;
using ProgressCallback = std::function<void(int64_t current, int64_t total)>;

using WebSocketServer = websocketpp::server<websocketpp::config::asio>;
using WebSocketConnectionPtr = websocketpp::connection_hdl;
using json = Json::Value;

/**
 * @brief FTP WebSocket服务器类
 */
class FTPWebSocketServer {
public:
    /**
     * @brief 构造函数
     * @param port WebSocket服务器端口
     */
    FTPWebSocketServer(uint16_t port = 9002);

    /**
     * @brief 启动服务器
     */
    void run();

    /**
     * @brief 停止服务器
     */
    void stop();

private:
    /**
     * @brief WebSocket连接打开时的回调
     */
    void onOpen(WebSocketConnectionPtr hdl);

    /**
     * @brief WebSocket连接关闭时的回调
     */
    void onClose(WebSocketConnectionPtr hdl);

    /**
     * @brief WebSocket消息处理回调
     */
    void onMessage(WebSocketConnectionPtr hdl, WebSocketServer::message_ptr msg);

    /**
     * @brief 处理FTP命令
     */
    void handleFTPCommand(WebSocketConnectionPtr hdl, const json& command);

    /**
     * @brief 发送响应给客户端
     */
    void sendResponse(WebSocketConnectionPtr hdl, const json& response);

    /**
     * @brief 进度回调函数
     */
    void onProgress(WebSocketConnectionPtr hdl, int64_t current, int64_t total);

private:
    WebSocketServer server;
    uint16_t port;
    std::map<void*, std::shared_ptr<FTPClient>> clientMap;
    std::mutex mutex;
};

} // namespace ftp

#endif // FTP_WEBSOCKET_H