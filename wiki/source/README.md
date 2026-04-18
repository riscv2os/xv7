# 原始碼參照

此目錄收錄 xv7/xv6 原始碼的檔案參照，用於快速查詢相關程式碼位置。

## xv6 核心 (xv6-riscv)

### 開機與初始化
- [kernel/main.c](../../kernel/main.c) - 核心進入點
- [kernel/entry.S](../../kernel/entry.S) - 開機入口

### 行程管理
- [kernel/proc.c](../../kernel/proc.c) - 行程實作
- [kernel/proc.h](../../kernel/proc.h) - 行程結構定義
- [kernel/swtch.S](../../kernel/swtch.S) - 上下文切換

### 記憶體管理
- [kernel/vm.c](../../kernel/vm.c) - 虛擬記憶體
- [kernel/kalloc.c](../../kernel/kalloc.c) - 實體記憶體配置
- [kernel/memlayout.h](../../kernel/memlayout.h) - 記憶體配置定義

### 檔案系統
- [kernel/fs.c](../../kernel/fs.c) - SFS 實作
- [kernel/fs.h](../../kernel/fs.h) - 檔案系統定義
- [kernel/bio.c](../../kernel/bio.c) - 緩衝區快取

### 陷阱處理
- [kernel/trap.c](../../kernel/trap.c) - 陷阱處理
- [kernel/trampoline.S](../../kernel/trampoline.S) - Trampoline 頁面

### 系統呼叫
- [kernel/syscall.c](../../kernel/syscall.c) - 系統呼叫分派
- [kernel/syscall.h](../../kernel/syscall.h) - 系統呼叫編號

### 裝置驅動
- [kernel/uart.c](../../kernel/uart.c) - UART 驅動
- [kernel/console.c](../../kernel/console.c) - 終端機
- [kernel/virtio_disk.c](../../kernel/virtio_disk.c) - VirtIO 磁碟

## xv7 網路功能

### 網路堆疊
- [kernel/net/net.c](../../kernel/net/net.c) - 網路核心
- [kernel/net/net.h](../../kernel/net/net.h) - 網路定義
- [kernel/net/ether.c](../../kernel/net/ether.c) - 乙太網路
- [kernel/net/ip.c](../../kernel/net/ip.c) - IP 通訊協定
- [kernel/net/tcp.c](../../kernel/net/tcp.c) - TCP 通訊協定
- [kernel/net/udp.c](../../kernel/net/udp.c) - UDP 通訊協定
- [kernel/net/arp.c](../../kernel/net/arp.c) - ARP 通訊協定
- [kernel/net/icmp.c](../../kernel/net/icmp.c) - ICMP 通訊協定

### 網路驅動
- [kernel/net/platform/xv6-riscv/virtio_net.c](../../kernel/net/platform/xv6-riscv/virtio_net.c) - VirtIO 網路驅動

### Socket API
- [kernel/syssocket.c](../../kernel/syssocket.c) - Socket 系統呼叫

## 使用者程式

- [user/init.c](../../user/init.c) - 第一個使用者行程
- [user/sh.c](../../user/sh.c) - Shell
- [user/tcpecho.c](../../user/tcpecho.c) - TCP Echo 範例
- [user/httpd.c](../../user/httpd.c) - HTTP 伺服器
- [user/ifconfig.c](../../user/ifconfig.c) - 網路設定指令

## 參見
- [xv6原始碼](../xv6原始碼.md) - xv6 原始碼說明
- [系統架構](../系統架構.md) - 系統架構總覽