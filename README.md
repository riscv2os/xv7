xv7
===

xv7 是基於 xv6-riscv 的 Unix 風格作業系統，額外新增了 TCP/IP 網路功能。

## 系統架構

```
┌─────────────────────────────────────────────────────────┐
│                    使用者空間                            │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐        │
│  │ tcpecho │ │ httpd   │ │ telnetd │ │ curl    │        │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘        │
├─────────────────────────────────────────────────────────┤
│                    系統呼叫                              │
│  open, read, write, fork, exec, pipe, socket, connect     │
├─────────────────────────────────────────────────────────┤
│                    核心空間                              │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐        │
│  │ 行程管理    │ │ 檔案系統    │ │ 網路堆疊    │        │
│  │             │ │ (SFS)       │ │             │        │
│  └─────────────┘ └─────────────┘ └─────────────┘        │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐        │
│  │ 記憶體管理  │ │ 裝置驅動    │ │ VirtIO      │        │
│  │             │ │            │ │ 網路/磁碟   │        │
│  └─────────────┘ └─────────────┘ └─────────────┘        │
└─────────────────────────────────────────────────────────┘
```

## 環境需求

- **Linux**：本專案必須在 Linux 環境下執行（macOS 不支援）
- **QEMU**：安裝 qemu-system-riscv64
- **RISC-V Toolchain**：交叉編譯工具

### 安裝方法（Ubuntu/Debian）

```shell
sudo apt update
sudo apt install -y build-essential gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf
sudo apt install -y qemu-system-misc
```

### 安裝方法（Fedora/RHEL）

```shell
sudo dnf install -y riscv64-unknown-elf-gcc riscv64-unknown-elf-binutils
sudo dnf install -y qemu-system-riscv
```

## 編譯與執行

```shell
make qemu          # 編譯核心 + fs.img，執行 QEMU（需要 sudo 建立 tap0）
make qemu NET=user   # 使用者模式網路，無需 sudo
make qemu NET=none   # 無網路設備
make clean        # 清除編譯產物
```

QEMU 預設使用 3 顆 CPU：`make qemu CPUS=1`

## 網路設定

在 xv7 內設定網路介面：
```shell
ifconfig net0 192.0.2.2 netmask 255.255.255.0
```

測試網路功能：
```shell
tcpecho    # 啟動 TCP Echo 伺服器（連接 port 7）
udpecho    # 啟動 UDP Echo 伺服器（連接 port 7）
```

## 元件位置

| 元件 | 路徑 |
|------|------|
| 行程管理 | `kernel/proc.c`, `kernel/proc.h` |
| 記憶體管理 | `kernel/vm.c`, `kernel/kalloc.c` |
| 檔案系統 | `kernel/fs.c`, `kernel/bio.c` |
| 網路堆疊 | `kernel/net/` |
| 裝置驅動 | `kernel/virtio_disk.c`, `kernel/uart.c` |

## 授權

xv6-riscv: MIT License
其他程式碼: MIT License