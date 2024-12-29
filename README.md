# FTP WebSocket服务器接口文档

通过此WebSocket API，前端客户端可以与FTP服务器交互，执行文件传输相关操作。本文档详细说明了WebSocket接口的用法，包括命令、参数和返回格式。

---

## WebSocket连接地址

**URL:** `ws://<服务器地址>:<端口>`  

- 将 `<服务器地址>` 替换为服务器的IP地址或域名。  
- 将 `<端口>` 替换为运行 `FTPWebSocketServer` 时指定的端口。

---

## 消息格式

### 请求消息

所有请求消息都使用JSON格式。

```json
{
  "cmd": "<命令名称>",
  "parameters": { /* 命令相关参数 */ }
}
```

### 响应消息

所有响应消息也使用JSON格式。

```json
jsonCopy code{
  "status": "success | error",
  "data": { /* 命令返回的数据 */ },
  "error": "<错误信息>" // 仅在 status 为 "error" 时存在
}
```

## 支持的命令

### 1. **连接FTP服务器**

连接到FTP服务器。

**命令名称：** `connect`
**参数：**

- `host` (字符串)：FTP服务器的主机名或IP地址。
- `port` (整数)：FTP服务器端口（默认值：21）。
- `useTLS` (布尔值，可选)：是否使用TLS（默认值：`false`）。
- `verify_peer` (布尔值，可选)：是否验证服务器证书（默认值：`true`）。
- `ca_file` (字符串，可选)：CA文件路径。
- `ca_path` (字符串，可选)：CA目录路径。
- `cert_file` (字符串，可选)：客户端证书文件路径。
- `key_file` (字符串，可选)：客户端私钥文件路径。

**请求示例：**

```json
jsonCopy code{
  "cmd": "connect",
  "host": "ftp.example.com",
  "port": 21,
  "useTLS": true
}
```

**响应示例：**

```json
jsonCopy code{
  "status": "success"
}
```

------

### 2. **登录**

登录FTP服务器。

**命令名称：** `login`
**参数：**

- `username` (字符串)：FTP用户名。
- `password` (字符串)：FTP密码。

**请求示例：**

```json
jsonCopy code{
  "cmd": "login",
  "username": "user",
  "password": "pass"
}
```

**响应示例：**

```json
jsonCopy code{
  "status": "success"
}
```

------

### 3. **列出文件**

列出当前目录的文件。

**命令名称：** `list`
**参数：** 无

**请求示例：**

```json
jsonCopy code{
  "cmd": "list"
}
```

**响应示例：**

```json
jsonCopy code{
  "status": "success",
  "files": [
    "file1.txt",
    "file2.txt"
  ]
}
```

------

### 4. **上传文件**

将本地文件上传到FTP服务器。

**命令名称：** `upload`
**参数：**

- `localPath` (字符串)：本地文件路径。
- `remotePath` (字符串)：服务器上的目标路径。
- `resume` (布尔值，可选)：是否断点续传（默认值：`false`）。

**请求示例：**

```json
jsonCopy code{
  "cmd": "upload",
  "localPath": "/local/file.txt",
  "remotePath": "/remote/file.txt",
  "resume": false
}
```

**响应示例：**

```json
jsonCopy code{
  "status": "success"
}
```

------

### 5. **下载文件**

从FTP服务器下载文件。

**命令名称：** `download`
**参数：**

- `remotePath` (字符串)：服务器上的文件路径。
- `localPath` (字符串)：保存到本地的路径。
- `resume` (布尔值，可选)：是否断点续传（默认值：`false`）。

**请求示例：**

```json
jsonCopy code{
  "cmd": "download",
  "remotePath": "/remote/file.txt",
  "localPath": "/local/file.txt",
  "resume": false
}
```

**响应示例：**

```json
jsonCopy code{
  "status": "success"
}
```

------

### 6. **获取当前目录**

获取FTP服务器上的当前工作目录。

**命令名称：** `pwd`
**参数：** 无

**请求示例：**

```json
jsonCopy code{
  "cmd": "pwd"
}
```

**响应示例：**

```json
jsonCopy code{
  "status": "success",
  "path": "/current/directory"
}
```

------

### 7. **更改目录**

更改当前工作目录。

**命令名称：** `cd`
**参数：**

- `path` (字符串)：目标目录路径。

**请求示例：**

```
jsonCopy code{
  "cmd": "cd",
  "path": "/new/directory"
}
```

**响应示例：**

```
jsonCopy code{
  "status": "success"
}
```

------

### 8. **创建目录**

在FTP服务器上创建新目录。

**命令名称：** `mkdir`
**参数：**

- `path` (字符串)：新目录路径。

**请求示例：**

```
jsonCopy code{
  "cmd": "mkdir",
  "path": "/new/directory"
}
```

**响应示例：**

```
jsonCopy code{
  "status": "success"
}
```

------

### 9. **删除文件**

删除FTP服务器上的文件。

**命令名称：** `delete`
**参数：**

- `path` (字符串)：目标文件路径。

**请求示例：**

```
jsonCopy code{
  "cmd": "delete",
  "path": "/remote/file.txt"
}
```

**响应示例：**

```
jsonCopy code{
  "status": "success"
}
```

------

### 10. **设置传输模式**

设置FTP的传输模式。

**命令名称：** `setTransferMode`
**参数：**

- `mode` (字符串)：传输模式，可为 `ACTIVE` 或 `PASSIVE`。

**请求示例：**

```
jsonCopy code{
  "cmd": "setTransferMode",
  "mode": "PASSIVE"
}
```

**响应示例：**

```
jsonCopy code{
  "status": "success"
}
```

------

### 11. **设置传输类型**

设置FTP的传输类型。

**命令名称：** `setTransferType`
**参数：**

- `type` (字符串)：传输类型，可为 `ASCII` 或 `BINARY`。

**请求示例：**

```
jsonCopy code{
  "cmd": "setTransferType",
  "type": "BINARY"
}
```

**响应示例：**

```
jsonCopy code{
  "status": "success"
}
```

------

### 12. **文件传输进度更新**

文件上传/下载时，服务器会发送进度更新消息。

**消息类型：** `progress`
**消息内容：**

```
jsonCopy code{
  "type": "progress",
  "current": 5000,
  "total": 10000,
  "percentage": 50
}
```

------

### 错误处理

错误响应消息的格式为：

```
jsonCopy code{
  "status": "error",
  "error": "错误描述信息"
}
```

**示例：**

```
jsonCopy code{
  "status": "error",
  "error": "Invalid command"
}
```
