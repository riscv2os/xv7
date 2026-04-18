# xv7 測試執行步驟

## 環境需求
- TUN/TAP 支援（/dev/net/tun）
- qemu-system-riscv64
- tmux

## 執行步驟

### 1. 設定 TAP 設備

```bash
# 密碼：082371461

# 檢查 tap0 是否存在，若不存在則創建
echo "082371461" | sudo -S ip link show tap0 2>/dev/null || echo "082371461" | sudo -S ip tuntap add mode tap name tap0

# 啟動 tap0 並設定 IP
echo "082371461" | sudo -S ip link set tap0 up
echo "082371461" | sudo -S ip addr add 192.0.2.1/24 dev tap0
```

### 2. 執行測試腳本

```bash
cd /mnt/macos/c0computer/os/xv7

# 網路基礎測試
timeout 25 bash net_test.sh

# HTTP 測試
timeout 30 bash http_test.sh

# Telnet 測試
timeout 25 bash telnet_test.sh
```

### 3. 測試完成後清理

```bash
# 關閉 tmux sessions
tmux kill-session -t xv6-net-test 2>/dev/null || true
tmux kill-session -t xv6-http-test 2>/dev/null || true
tmux kill-session -t xv6-telnet-test 2>/dev/null || true

# 關閉 qemu
pkill -9 -f qemu-system-riscv64
```

## 預期結果

- **net_test.sh**: UDP Echo 運作正常
- **http_test.sh**: HTTP 伺服器正常運作，可從外部 curl 存取
- **telnet_test.sh**: Telnet 伺服器啟動成功