# 書籍編輯工作經驗

## 任務概述

本專案有兩個專案名稱：
- **xv6-riscv-net**：pandax381 的原始專案，基於 xv6-riscv 新增 TCP/IP 網路功能
- **xv7**：從 xv6-riscv-net 分支我們的專案，新增 httpd、curl、telnetd

## 命名原則

- 提到 pandax381 的原始專案時 → 使用 xv6-riscv-net
- 提到我們的專案時 → 使用 xv7

## 搜尋與修正流程

### 1. 找出所有提及專案名稱的位置

```bash
# 搜尋 xv6-riscv-net
grep -r "xv6-riscv-net" /mnt/macos/c0computer/os/xv7

# 搜尋 xv7
grep -r "xv7" /mnt/macos/c0computer/os/xv7-book
```

### 2. 判斷哪些需要修正

- **測試腳本** (*.sh)：是我們專案的測試，應使用 xv7
- **README.md**：是我們的專案，應使用 xv7（但 git clone 指令保留原始專案名稱）
- **書籍內容**：描述關係時，正確使用 xv6-riscv-net（如「基於 xv6-riscv-net 的擴充版本」）
- **歷史記錄** (ccc_/ 目錄)：保持原樣，記錄當時的名稱

### 3. 常見修正

測試腳本的標題：
```bash
# 錯誤
echo "=== xv6-riscv-net 網路自動測試 ==="

# 正確
echo "=== xv7 網路自動測試 ==="
```

## 測試執行前置作業

### TAP 設備設定

```bash
# 密碼：082371461

# 檢查 tap0 是否存在
ip link show tap0 2>/dev/null || echo "082371461" | sudo -S ip tuntap add mode tap name tap0

# 啟動 tap0 並設定 IP
echo "082371461" | sudo -S ip link set tap0 up
echo "082371461" | sudo -S ip addr add 192.0.2.1/24 dev tap0
```

### 執行測試

```bash
cd /mnt/macos/c0computer/os/xv7
timeout 25 bash net_test.sh
timeout 30 bash http_test.sh
timeout 25 bash telnet_test.sh
```

## 書籍章節修改記錄

### 第三章新增 telnetd.c

1. 在 3.2 新增 3.2.3 Telnet 伺服器章節
2. 更新 3.3 程式碼結構，加入 telnetd.c
3. 更新 3.4 編譯方式，加入 $U/_telnetd
4. 更新 3.5 使用方式，新增 Telnet 伺服器說明