#include "ftpwebsocket.h"
#include "ftpclient.h"
#include <iostream>
#include <vector>
#include <string>

namespace ftp {

FTPWebSocketServer::FTPWebSocketServer(uint16_t port) : port(port) {
    // 配置 WebSocket 服务器
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.set_access_channels(websocketpp::log::alevel::connect);
    server.set_access_channels(websocketpp::log::alevel::disconnect);
    server.set_access_channels(websocketpp::log::alevel::app);

    // 初始化 Asio
    server.init_asio();

    // 设置回调函数
    server.set_open_handler(std::bind(&FTPWebSocketServer::onOpen, this, std::placeholders::_1));
    server.set_close_handler(std::bind(&FTPWebSocketServer::onClose, this, std::placeholders::_1));
    server.set_message_handler(std::bind(&FTPWebSocketServer::onMessage, this, 
        std::placeholders::_1, std::placeholders::_2));
}

void FTPWebSocketServer::run() {
    try {
        // 监听指定端口
        server.listen(port);

        // 开始接受连接
        server.start_accept();

        std::cout << "WebSocket server running on port " << port << std::endl;

        // 运行服务器
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "WebSocket server error: " << e.what() << std::endl;
    }
}

void FTPWebSocketServer::stop() {
    server.stop_listening();

    for (auto& pair : clientMap) {
        if (pair.second) {
            pair.second->disconnect();
        }
    }
    clientMap.clear();
}

void FTPWebSocketServer::onOpen(WebSocketConnectionPtr hdl) {
    std::lock_guard<std::mutex> lock(mutex);
    auto conn = server.get_con_from_hdl(hdl);
    auto raw_hdl = hdl.lock().get();

    // 为新连接创建 FTP 客户端实例
    clientMap[raw_hdl] = std::make_shared<FTPClient>();

    std::cout << "Client connected" << std::endl;
}

void FTPWebSocketServer::onClose(WebSocketConnectionPtr hdl) {
    std::lock_guard<std::mutex> lock(mutex);
    auto raw_hdl = hdl.lock().get();

    // 清理连接相关资源
    if (clientMap.find(raw_hdl) != clientMap.end()) {
        clientMap[raw_hdl]->disconnect();
        clientMap.erase(raw_hdl);
    }

    std::cout << "Client disconnected" << std::endl;
}

void FTPWebSocketServer::onMessage(WebSocketConnectionPtr hdl, WebSocketServer::message_ptr msg) {
    try {
        // 解析 JSON 消息
        Json::Reader reader;
        json command;
        if (!reader.parse(msg->get_payload(), command)) {
            json response;
            response["status"] = "error";
            response["error"] = "Invalid JSON format";
            sendResponse(hdl, response);
            return;
        }

        // 处理 FTP 命令
        handleFTPCommand(hdl, command);
    } catch (const std::exception& e) {
        json response;
        response["status"] = "error";
        response["error"] = e.what();
        sendResponse(hdl, response);
    }
}

void FTPWebSocketServer::handleFTPCommand(WebSocketConnectionPtr hdl, const json& command) {
    auto raw_hdl = hdl.lock().get();
    auto client = clientMap[raw_hdl];

    if (!command.isMember("cmd")) {
        json response;
        response["status"] = "error";
        response["error"] = "Missing command";
        sendResponse(hdl, response);
        return;
    }

    std::string cmd = command["cmd"].asString();
    json response;

    try {
        if (cmd == "connect") {
            // ---- 从 JSON 中读取连接参数 ----
            std::string host = command["host"].asString();
            uint16_t port = command["port"].asUInt();

            // 是否使用 TLS/SSL，可选字段，默认 false
            bool useTLS = command.get("useTLS", false).asBool(); 
            
            // 如果需要TLS，则可选地从前端设置各项 TLS 配置
            if (useTLS) {
                // 是否验证服务器证书，默认 true
                client->tlsConfig.verify_peer = command.get("verify_peer", true).asBool();
                // CA 文件（若有）
                if (command.isMember("ca_file")) {
                    client->tlsConfig.ca_file = command["ca_file"].asString();
                }
                // CA 目录（若有）
                if (command.isMember("ca_path")) {
                    client->tlsConfig.ca_path = command["ca_path"].asString();
                }
                // 客户端证书（若有）
                if (command.isMember("cert_file")) {
                    client->tlsConfig.cert_file = command["cert_file"].asString();
                }
                // 客户端私钥（若有）
                if (command.isMember("key_file")) {
                    client->tlsConfig.key_file = command["key_file"].asString();
                }
            }

            // ---- 先进行普通的连接 ----
            if (!client->connect(host, port)) {
                response["status"] = "error";
                response["error"] = client->getLastError();
                sendResponse(hdl, response);
                return;
            }

            // ---- 如果需要 TLS，则进行 TLS 初始化和升级 ----
            if (useTLS) {
                // 初始化 SSL
                if (!client->initSSL()) {
                    response["status"] = "error";
                    response["error"] = client->getLastError();
                    // 出错后断开连接，清理资源
                    client->disconnect();
                    sendResponse(hdl, response);
                    return;
                }
                // 升级控制连接到 TLS
                if (!client->upgradeToTLS()) {
                    response["status"] = "error";
                    response["error"] = client->getLastError();
                    // 出错后断开连接，清理资源
                    client->disconnect();
                    sendResponse(hdl, response);
                    return;
                }
            }

            response["status"] = "success";

        } else if (cmd == "login") {
            std::string username = command["username"].asString();
            std::string password = command["password"].asString();

            if (client->login(username, password)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "list") {
            auto files = client->listFiles();
            response["status"] = "success";
            response["files"] = Json::Value(Json::arrayValue);
            for (const auto& file : files) {
                response["files"].append(file);
            }

        } else if (cmd == "upload") {
            std::string localPath = command["localPath"].asString();
            std::string remotePath = command["remotePath"].asString();
            bool resume = command.get("resume", false).asBool();

            auto progressCallback = std::bind(&FTPWebSocketServer::onProgress,
                                              this, hdl,
                                              std::placeholders::_1,
                                              std::placeholders::_2);

            if (client->uploadFile(localPath, remotePath, resume, progressCallback)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "download") {
            std::string remotePath = command["remotePath"].asString();
            std::string localPath = command["localPath"].asString();
            bool resume = command.get("resume", false).asBool();

            auto progressCallback = std::bind(&FTPWebSocketServer::onProgress,
                                              this, hdl,
                                              std::placeholders::_1,
                                              std::placeholders::_2);

            if (client->downloadFile(remotePath, localPath, resume, progressCallback)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "pwd") {
            std::string currentDir = client->getCurrentDir();
            if (!currentDir.empty()) {
                response["status"] = "success";
                response["path"] = currentDir;
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "cd") {
            std::string path = command["path"].asString();
            if (client->changeDir(path)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "mkdir") {
            std::string path = command["path"].asString();
            if (client->makeDir(path)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "rmdir") {
            std::string path = command["path"].asString();
            if (client->removeDir(path)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "delete") {
            std::string path = command["path"].asString();
            if (client->deleteFile(path)) {
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = client->getLastError();
            }

        } else if (cmd == "setTransferMode") {
            std::string mode = command["mode"].asString();
            if (mode == "ACTIVE" || mode == "PASSIVE") {
                client->setTransferMode(mode == "ACTIVE" 
                                         ? TransferMode::ACTIVE 
                                         : TransferMode::PASSIVE);
                response["status"] = "success";
            } else {
                response["status"] = "error";
                response["error"] = "Invalid transfer mode";
            }

        } else if (cmd == "setTransferType") {
            std::string type = command["type"].asString();
            if (type == "ASCII" || type == "BINARY") {
                if (client->setTransferType(type == "ASCII" 
                                            ? TransferType::ASCII 
                                            : TransferType::BINARY)) {
                    response["status"] = "success";
                } else {
                    response["status"] = "error";
                    response["error"] = client->getLastError();
                }
            } else {
                response["status"] = "error";
                response["error"] = "Invalid transfer type";
            }

        } else {
            response["status"] = "error";
            response["error"] = "Unknown command: " + cmd;
        }
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = e.what();
    }

    // 将处理结果发送回客户端
    sendResponse(hdl, response);
}

void FTPWebSocketServer::sendResponse(WebSocketConnectionPtr hdl, const json& response) {
    try {
        Json::FastWriter writer;
        std::string message = writer.write(response);
        server.send(hdl, message, websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        std::cerr << "Error sending response: " << e.what() << std::endl;
    }
}

void FTPWebSocketServer::onProgress(WebSocketConnectionPtr hdl, int64_t current, int64_t total) {
    json progress;
    progress["type"] = "progress";
    progress["current"] = static_cast<Json::Int64>(current);
    progress["total"] = static_cast<Json::Int64>(total);
    progress["percentage"] = (total > 0) ? (current * 100 / total) : 0;

    sendResponse(hdl, progress);
}

} // namespace ftp
