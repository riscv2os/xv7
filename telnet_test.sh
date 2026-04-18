#!/bin/bash

xv6_DIR="$(cd "$(dirname "$0")" && pwd)"
TMUX_SESSION="xv6-telnet-test"

echo "=== xv7 Telnet 測試 ==="

# 清理舊程序
echo "[1/4] 清理舊的程序..."
pkill -9 -f "qemu-system-riscv64" 2>/dev/null || true
sleep 1

# 設定 tap0
echo "[2/4] 設定 tap0 網路..."
ip link show tap0 2>/dev/null | grep -q "UP" || echo "警告: tap0 未啟動"

# 啟動 xv6
echo "[3/4] 啟動 xv6 qemu..."
tmux kill-session -t $TMUX_SESSION 2>/dev/null || true
sleep 1
tmux new-session -d -s $TMUX_SESSION
tmux send-keys -t $TMUX_SESSION "cd $xv6_DIR && make qemu" C-m

echo "[4/4] 等待開機並啟動 telnetd..."
sleep 10

tmux send-keys -t $TMUX_SESSION "ifconfig net0 192.0.2.2/24" C-m
sleep 1
tmux send-keys -t $TMUX_SESSION "telnetd" C-m
sleep 2

echo ""
echo "=== 測試 telnet 連線 ==="
echo "使用 telnet 測試："
echo "  telnet 192.0.2.2 23"
echo ""
echo "或使用 nc 測試："
echo "  nc 192.0.2.2 23"
echo ""
echo "=== telnetd 輸出 ==="
tmux capture-pane -t $TMUX_SESSION -p | grep -E "(Starting Telnet|telnetd|waiting)" | head -5

echo ""
echo "=== 清理 ==="
echo "測試完成後執行以下指令關閉:"
echo "  tmux kill-session -t $TMUX_SESSION"

trap "tmux kill-session -t $TMUX_SESSION 2>/dev/null; exit 0" INT
wait