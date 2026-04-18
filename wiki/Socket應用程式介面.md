# Socket應用程式介面

xv7 實作了 BSD 風格的 Socket 介面，允許使用者程式進行網路通訊。這是基於 xv6 新增的功能。

## Socket 架構

```c
struct socket {
    struct sock *so;           // 通訊端點
    struct file *file;         // 關聯的檔案結構
};
```

## 系統呼叫

### socket()

建立新的 Socket：

```c
int socket(int domain, int type, int protocol);
// domain: AF_INET (IPv4)
// type: SOCK_STREAM (TCP), SOCK_DGRAM (UDP)
// protocol: 0 (自動選擇)
// 回傳：檔案描述符
```

### bind()

將 Socket 綁定到位址：

```c
int bind(int sockfd, struct sockaddr *addr, socklen_t addrlen);
// 回傳：0 成功，-1 失敗
```

### listen()

監聽連線要求（僅 TCP）：

```c
int listen(int sockfd, int backlog);
// backlog: 最大待處理連線數
// 回傳：0 成功，-1 失敗
```

### accept()

接受連線（僅 TCP）：

```c
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
// 回傳：新 Socket 檔案描述符
```

### connect()

連線至遠端（僅 TCP）：

```c
int connect(int sockfd, struct sockaddr *addr, socklen_t addrlen);
// 回傳：0 成功，-1 失敗
```

### send()

傳送資料：

```c
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
// 回傳：傳送位元組數
```

### recv()

接收資料：

```c
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
// 回傳：接收位元組數
```

### close()

關閉 Socket：

```c
int close(int sockfd);
// 回傳：0 成功，-1 失敗
```

## Socket 選項

支援常見的 Socket 選項：
- `SO_REUSEADDR` - 重複使用位址
- `SO_KEEPALIVE` - 保持連線
- `TCP_NODELAY` - 停用 Nagle 演算法

## 使用範例

### TCP Echo 伺服器

```c
int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    bind(s, (struct sockaddr *)&addr, sizeof(addr));
    listen(s, 1);
    
    while (1) {
        int c = accept(s, 0, 0);
        // 處理連線...
        close(c);
    }
}
```

### TCP 用戶端

```c
int s = socket(AF_INET, SOCK_STREAM, 0);
struct sockaddr_in addr = {0};
addr.sin_family = AF_INET;
addr.sin_port = htons(80);
addr.sin_addr.s_addr = inet_addr("192.0.2.2");

connect(s, (struct sockaddr *)&addr, sizeof(addr));
send(s, "Hello", 5, 0);
close(s);
```

## 相關檔案

- `kernel/syssocket.c` - Socket 系統呼叫實作
- `kernel/net/socket.c` - Socket 核心實作
- `kernel/net/socket.h` - Socket 標頭定義

## 參見
- [網路堆疊](網路堆疊.md)
- [系統架構](系統架構.md)