#include "ftpclient.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <memory>

using namespace ftp;

// 打印进度条
void printProgress(const std::string& prefix, int64_t current, int64_t total) {
    const int barWidth = 50;
    float progress = static_cast<float>(current) / total;
    int pos = static_cast<int>(barWidth * progress);

    std::cout << prefix << " [";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "% "
              << "(" << current << "/" << total << " bytes)\r";
    std::cout.flush();

    if (current >= total) {
        std::cout << std::endl;
    }
}

// 测试基本功能
void testBasicFunctions(FTPClient& client) {
    std::cout << "\n=== Testing Basic FTP Functions ===\n";

    // 获取当前目录
    std::cout << "\nGetting current directory...\n";
    std::string currentDir = client.getCurrentDir();
    if (!currentDir.empty()) {
        std::cout << "Current directory: " << currentDir << std::endl;
    } else {
        std::cout << "Failed to get current directory: " << client.getLastError() << std::endl;
        return;  // 如果基本操作失败，提前返回
    }

    // 列出文件
    std::cout << "\nListing files in current directory...\n";
    auto files = client.listFiles();
    if (!files.empty()) {
        std::cout << "Files found:\n";
        for (const auto& file : files) {
            std::cout << file << std::endl;
        }
    } else {
        std::cout << "No files found or error occurred: " << client.getLastError() << std::endl;
    }

    // 创建测试目录
    std::cout << "\nCreating test directory...\n";
    std::string testDir = "test_dir_" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    if (client.makeDir(testDir)) {
        std::cout << "Directory created successfully\n";
        
        // 切换到测试目录
        std::cout << "Changing to test directory...\n";
        if (client.changeDir(testDir)) {
            std::cout << "Changed directory successfully\n";

            // 列出新目录中的文件
            std::cout << "\nListing files in new directory...\n";
            files = client.listFiles();
            if (files.empty()) {
                std::cout << "Directory is empty (as expected)\n";
            }

            // 返回上级目录
            std::cout << "\nChanging back to parent directory...\n";
            if (client.changeDir("..")) {
                std::cout << "Changed back successfully\n";
                
                // 删除测试目录
                std::cout << "\nRemoving test directory...\n";
                if (client.removeDir(testDir)) {
                    std::cout << "Directory removed successfully\n";
                } else {
                    std::cout << "Failed to remove directory: " << client.getLastError() << std::endl;
                }
            }
        } else {
            std::cout << "Failed to change directory: " << client.getLastError() << std::endl;
        }
    } else {
        std::cout << "Failed to create directory: " << client.getLastError() << std::endl;
    }
}

// 测试文件传输
void testFileTransfer(FTPClient& client) {
    std::cout << "\n=== Testing File Transfer ===\n";

    // 创建测试文件
    const std::string localFile = "test_upload.txt";
    const std::string remoteFile = "test_upload.txt";
    const std::string downloadFile = "test_download.txt";

    // 创建本地测试文件
    {
        std::ofstream file(localFile);
        file << "This is a test file for FTP upload.\n";
        file << "It contains some test data.\n";
        for (int i = 0; i < 100; ++i) {
            file << "Line " << i << ": Some random test data...\n";
        }
    }

    // 测试ASCII模式传输
    std::cout << "\nTesting ASCII mode transfer...\n";
    if (!client.setTransferType(TransferType::ASCII)) {
        std::cout << "Failed to set ASCII mode: " << client.getLastError() << std::endl;
        return;
    }

    // 上传文件（带进度显示）
    std::cout << "\nUploading test file in ASCII mode...\n";
    if (!client.uploadFile(localFile, remoteFile, false, 
        [](int64_t current, int64_t total) {
            printProgress("Upload progress", current, total);
        })) {
        std::cout << "Failed to upload file: " << client.getLastError() << std::endl;
        return;
    }
    std::cout << "File uploaded successfully\n";

    // 测试二进制模式传输
    std::cout << "\nTesting BINARY mode transfer...\n";
    if (!client.setTransferType(TransferType::BINARY)) {
        std::cout << "Failed to set BINARY mode: " << client.getLastError() << std::endl;
        return;
    }

    // 下载文件（带进度显示）
    std::cout << "\nDownloading test file in BINARY mode...\n";
    if (!client.downloadFile(remoteFile, downloadFile, false,
        [](int64_t current, int64_t total) {
            printProgress("Download progress", current, total);
        })) {
        std::cout << "Failed to download file: " << client.getLastError() << std::endl;
        return;
    }
    std::cout << "File downloaded successfully\n";

    // 验证文件内容
    std::cout << "\nVerifying file content...\n";
    {
        std::ifstream file1(localFile, std::ios::binary);
        std::ifstream file2(downloadFile, std::ios::binary);
        std::string content1((std::istreambuf_iterator<char>(file1)),
                           std::istreambuf_iterator<char>());
        std::string content2((std::istreambuf_iterator<char>(file2)),
                           std::istreambuf_iterator<char>());

        if (content1 == content2) {
            std::cout << "File content verification: SUCCESS\n";
        } else {
            std::cout << "File content verification: FAILED\n";
        }
    }

    // 测试断点续传
    std::cout << "\nTesting resume capability...\n";
    {
        // 只下载一半
        std::ofstream partial(downloadFile, std::ios::binary);
        std::ifstream source(localFile, std::ios::binary);
        source.seekg(0, std::ios::end);
        auto size = source.tellg();
        source.seekg(0);
        std::vector<char> buffer(size / 2);
        source.read(buffer.data(), buffer.size());
        partial.write(buffer.data(), buffer.size());
        partial.close();

        // 续传剩余部分
        std::cout << "Resuming download...\n";
        if (!client.downloadFile(remoteFile, downloadFile, true,
            [](int64_t current, int64_t total) {
                printProgress("Resume progress", current, total);
            })) {
            std::cout << "Failed to resume download: " << client.getLastError() << std::endl;
        } else {
            std::cout << "Resume completed successfully\n";
        }
    }

    // 清理：删除远程文件
    std::cout << "\nCleaning up: deleting remote file...\n";
    if (client.deleteFile(remoteFile)) {
        std::cout << "Remote file deleted successfully\n";
    } else {
        std::cout << "Failed to delete remote file: " << client.getLastError() << std::endl;
    }

    // 清理本地测试文件
    std::remove(localFile.c_str());
    std::remove(downloadFile.c_str());
}

int main() {
    std::string host, username, password;
    uint16_t port = 21;
    bool useTLS = false;

    // 获取连接信息
    std::cout << "Enter FTP server host: ";
    std::getline(std::cin, host);

    std::cout << "Enter username: ";
    std::getline(std::cin, username);

    std::cout << "Enter password: ";
    std::getline(std::cin, password);

    std::cout << "Use TLS? (y/n): ";
    std::string response;
    std::getline(std::cin, response);
    useTLS = (response == "y" || response == "Y");

    // 创建FTP客户端实例
    FTPClient client;

    std::string certPath, keyPath, caPath;
    
    if (useTLS) {
        // 获取证书路径
        std::cout << "Enter path to client certificate (client.crt): ";
        std::getline(std::cin, certPath);
        if (certPath.empty()) certPath = "client.crt";

        std::cout << "Enter path to client private key (client.key): ";
        std::getline(std::cin, keyPath);
        if (keyPath.empty()) keyPath = "client.key";

        std::cout << "Enter path to CA certificate (ca.crt): ";
        std::getline(std::cin, caPath);
        if (caPath.empty()) caPath = "ca.crt";

        // 配置TLS
        client.tlsConfig.verify_peer = true;  // 启用证书验证
        client.tlsConfig.ca_file = caPath;    // 设置CA证书路径
        client.tlsConfig.cert_file = certPath; // 设置客户端证书路径
        client.tlsConfig.key_file = keyPath;   // 设置客户端私钥路径
        
        // 初始化SSL
        std::cout << "\nInitializing SSL...\n";
        if (!client.initSSL()) {
            std::cerr << "Failed to initialize SSL: " << client.getLastError() << std::endl;
            return 1;
        }
        
        // 显示TLS配置信息
        std::cout << "SSL Configuration:\n";
        std::cout << "- Certificate verification: enabled\n";
        std::cout << "- Client certificate: " << certPath << "\n";
        std::cout << "- Client private key: " << keyPath << "\n";
        std::cout << "- CA certificate: " << caPath << "\n";
    }

    // 连接到服务器
    std::cout << "\nConnecting to " << host << ":" << port << "...\n";
    if (!client.connect(host, port)) {
        std::cerr << "Failed to connect: " << client.getLastError() << std::endl;
        return 1;
    }
    std::cout << "Connected successfully\n";

    if (useTLS) {
        // 升级到TLS连接
        std::cout << "\nUpgrading to TLS...\n";
        if (!client.upgradeToTLS()) {
            std::cerr << "Failed to upgrade to TLS: " << client.getLastError() << std::endl;
            std::cerr << "Note: If using a self-signed certificate or local server, try:\n";
            std::cerr << "1. Use a valid SSL certificate\n";
            std::cerr << "2. Or disable certificate verification (for testing only)\n";
            client.disconnect();
            return 1;
        }
        std::cout << "TLS connection established\n";
        
        // 显示连接信息
        std::cout << "SSL/TLS Connection Info:\n";
        std::string sslInfo = client.getSSLInfo();
        if (!sslInfo.empty()) {
            std::cout << sslInfo << std::endl;
        }
    }

    // 登录
    std::cout << "\nLogging in...\n";
    if (!client.login(username, password)) {
        std::cerr << "Failed to login: " << client.getLastError() << std::endl;
        client.disconnect();
        return 1;
    }
    std::cout << "Logged in successfully\n";

    // 设置被动模式
    client.setTransferMode(TransferMode::PASSIVE);

    // 登录成功后设置传输模式
    std::cout << "\nSetting transfer mode...\n";
    std::cout << "Select transfer mode (1: Passive, 2: Active): ";
    std::string modeChoice;
    std::getline(std::cin, modeChoice);
    
    if (modeChoice == "1") {
        client.setTransferMode(TransferMode::PASSIVE);
        std::cout << "Using passive mode\n";
    } else if (modeChoice == "2") {
        client.setTransferMode(TransferMode::ACTIVE);
        std::cout << "Using active mode\n";
    } else {
        std::cout << "Invalid choice, defaulting to passive mode\n";
        client.setTransferMode(TransferMode::PASSIVE);
    }

    try {
        // 测试基本功能
        testBasicFunctions(client);

        // 测试文件传输
        testFileTransfer(client);

    } catch (const std::exception& e) {
        std::cerr << "\nError occurred: " << e.what() << std::endl;
    }

    // 断开连接
    std::cout << "\nDisconnecting...\n";
    client.disconnect();
    std::cout << "Disconnected from server\n";

    return 0;
}