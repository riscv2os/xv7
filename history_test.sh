#!/bin/bash

set -e

xv6_DIR="$(cd "$(dirname "$0")" && pwd)"
QEMU_BIN="qemu-system-riscv64"

echo "=== xv7 history 測試 ==="

TMP_IMG="$(mktemp -t xv6-fs.XXXXXX)"
cp "$xv6_DIR/fs.img" "$TMP_IMG"

pkill -9 -f "$QEMU_BIN" 2>/dev/null || true
sleep 1

cd "$xv6_DIR"
make TOOLPREFIX=riscv64-unknown-elf- kernel/kernel fs.img

QEMU_OPTS="-machine virt -bios none -kernel $xv6_DIR/kernel/kernel -m 128M -smp 1 -nographic"
QEMU_OPTS+=" -global virtio-mmio.force-legacy=false"
QEMU_OPTS+=" -drive file=$TMP_IMG,if=none,format=raw,id=x0"
QEMU_OPTS+=" -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"
QEMU_OPTS+=" -netdev user,id=en0"
QEMU_OPTS+=" -device virtio-net-device,netdev=en0,csum=off,gso=off,guest_csum=off,bus=virtio-mmio-bus.1"

# Create a new tmux session that we'll use
TMUX_SESSION="xv7-history-$$"

# Make sure we don't have this session
tmux kill-session -t "$TMUX_SESSION" 2>/dev/null || true

# Create session with QEMU running
tmux new-session -d -s "$TMUX_SESSION" "$QEMU_BIN $QEMU_OPTS"

# Wait for boot
sleep 8

# Run commands
tmux send-keys -t "$TMUX_SESSION" "echo hello" C-m
sleep 1
tmux send-keys -t "$TMUX_SESSION" "echo world" C-m
sleep 1
tmux send-keys -t "$TMUX_SESSION" "ls" C-m  
sleep 1
tmux send-keys -t "$TMUX_SESSION" "history" C-m
sleep 3

# Refresh the pane
tmux send-keys -t "$TMUX_SESSION" C-l
sleep 1

# Capture output
OUTPUT=$(tmux capture-pane -t "$TMUX_SESSION" -p -e)

echo "===== CAPTURED OUTPUT ====="
echo "$OUTPUT"
echo "==========================="

# Check for success
PASS=0

if echo "$OUTPUT" | grep -q "echo hello"; then
    echo "PASS: found 'echo hello'"
    PASS=$((PASS+1))
else
    echo "FAIL: did not find 'echo hello'"
fi

if echo "$OUTPUT" | grep -q "echo world"; then
    echo "PASS: found 'echo world'"
    PASS=$((PASS+1))
else
    echo "FAIL: did not find 'echo world'"
fi

if echo "$OUTPUT" | grep -q "ls"; then
    echo "PASS: found 'ls'"
    PASS=$((PASS+1))
else
    echo "FAIL: did not find 'ls'"
fi

if [ $PASS -eq 3 ]; then
    echo ""
    echo "=== All tests passed ==="
else
    echo ""
    echo "=== Some tests failed ==="
    exit 1
fi

# Cleanup
tmux kill-session -t "$TMUX_SESSION" 2>/dev/null || true
rm -f "$TMP_IMG"
