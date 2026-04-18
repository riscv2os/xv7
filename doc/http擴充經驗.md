# xv6-riscv-net HTTP 擴充經驗

## 概述

本文記錄如何在 xv6-riscv-net 作業系統上擴充 HTTP 伺服器和客戶端功能。

## 環境建置

### 必要工具
- RISC-V cross-compiler: `riscv64-linux-gnu-gcc`
- QEMU: `qemu-system-riscv64`
- tmux: 用於自動化管理 qemu 會話

### 網路設定
```bash
# 建立 tap0 網路介面
sudo ip tuntap add mode tap user $USER name tap0
sudo ip addr add 192.0.2.1/24 dev tap0
sudo ip link set tap0 up
```

## httpd.c - HTTP 伺服器

### 程式碼
位於 `user/httpd.c`

### 功能
- 監聽 TCP port 8080（可自訂）
- 接受 HTTP GET 請求
- 回傳簡單 HTML 頁面 `<h1>Hello!</h1>`

### 編譯與執行
1. 將 `_httpd` 加入 `Makefile` 的 `UPROGS`
2. 重新編譯：`make TOOLPREFIX=riscv64-linux-gnu-`
3. 在 xv6 中執行：`httpd`

### 程式碼重點
```c
// HTTP 回應格式
static char HTTP_RESPONSE[] = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "<h1>Hello!</h1>";
```

### 注意事項
- xv6 不支援 `const char*`，需使用 `char[]`
- xv6 標準函式庫有限，使用手動組裝字串
- HTTP 請求判斷需避免使用 `strncmp`（需自行宣告）

## curl.c - HTTP 客戶端

### 程式碼
位於 `user/curl.c`

### 功能
- 建立 TCP 連線到指定主機:port
- 發送 HTTP GET 請求
- 顯示伺服器回應

### 使用方式
```bash
curl 192.0.2.2 8080
```

### 程式碼重點
```c
// 手動組裝 HTTP 請求
i = 0;
request[i++] = 'G';
request[i++] = 'E';
request[i++] = 'T';
request[i++] = ' ';
// ... 組裝完整 HTTP 1.1 請求
request[i++] = '\r';
request[i++] = '\n';
// ...

connect(soc, (struct sockaddr *)&server, sizeof(server));
send(soc, request, i);
recv(soc, buf, sizeof(buf) - 1);
```

## 測試脚本

### nettest.sh - 基礎網路測試
```bash
./nettest.sh
```
測試 UDP Echo 功能：
- 啟動 xv6 qemu
- 設定網路 IP (192.0.2.2/24)
- 啟動 udpecho 伺服器
- 使用 nc 傳送測試訊息

### httptest.sh - HTTP 功能測試
```bash
./httptest.sh
```
測試 HTTP 功能：
- 啟動 xv6 qemu
- 設定網路 IP
- 啟動 httpd 伺服器
- 使用 curl 測試 HTTP 連線

## 常見問題

### 1. tap0 忙碌
```
t/tun (tap0): Device or resource busy
```
解決：先殺掉舊的 qemu 程序
```bash
pkill -9 -f qemu-system-riscv64
```

### 2. 編譯錯誤 - const 限定符
```
error: passing argument 2 of 'send' discards 'const' qualifier
```
解決：將 `const char*` 改為 `char[]`

### 3. 編譯錯誤 - 隱含宣告
```
error: implicit declaration of function 'strncmp'
```
解決：使用手動字元比對取代標準函式庫

### 4. tmux 會話忙碌
解決：先清理舊會話
```bash
tmux kill-session -t xv6-test
```

## 測試結果

```
=== 外部 curl 測試 (從主機) ===
<h1>Hello!</h1>
```

HTTP 伺服器和客戶端功能運作正常。

## 檔案列表

- `user/httpd.c` - HTTP 伺服器原始碼
- `user/curl.c` - HTTP 客戶端原始碼  
- `nettest.sh` - 網路測試脚本
- `httptest.sh` - HTTP 測試脚本