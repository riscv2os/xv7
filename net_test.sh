#!/bin/bash

xv6_DIR="$(cd "$(dirname "$0")" && pwd)"
TMUX_SESSION="xv6-net-test"

echo "=== xv7 網路自動測試 ==="

# 0. 清理可能存在的 qemu 程序
echo "[0/6] 清理舊的程序..."
pkill -9 -f "qemu-system-riscv64" 2>/dev/null || true
sleep 1

# 1. 設定 tap0 網路
echo "[1/6] 設定 tap0 網路介面..."
ip link show tap0 2>/dev/null | grep -q "UP" || echo "警告: tap0 未啟動，請先執行: sudo ip link set tap0 up"

# 2. 清理舊的 tmux session
echo "[2/6] 清理舊的 session..."
tmux kill-session -t $TMUX_SESSION 2>/dev/null || true

# 3. 啟動 qemu in tmux
echo "[3/6] 啟動 xv6 qemu..."
tmux new-session -d -s $TMUX_SESSION
tmux send-keys -t $TMUX_SESSION "cd $xv6_DIR && make qemu" C-m

# 4. 等待 xv6 啟動
echo "[4/6] 等待 xv6 開機..."
sleep 8

# 5. 設定網路
echo "[5/6] 設定 xv6 網路..."
tmux send-keys -t $TMUX_SESSION "ifconfig net0 192.0.2.2/24" C-m
sleep 1
tmux send-keys -t $TMUX_SESSION "udpecho" C-m
sleep 1

# 6. 測試網路
echo "[6/6] 測試 UDP Echo..."
echo "測試訊息" | nc -u -w 3 192.0.2.2 7 &
NC_PID=$!
sleep 2
kill $NC_PID 2>/dev/null || true

# 顯示結果
echo ""
echo "=== 測試結果 ==="
tmux capture-pane -t $TMUX_SESSION -p | tail -10

echo ""
echo "=== 清理 ==="
echo "按 Ctrl+C 結束測試，完成後執行以下指令關閉 qemu:"
echo "  tmux kill-session -t $TMUX_SESSION"

# 等待使用者中斷
trap "tmux kill-session -t $TMUX_SESSION 2>/dev/null; exit 0" INT
wait