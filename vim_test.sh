#!/bin/bash

set -e

xv6_DIR="$(cd "$(dirname "$0")" && pwd)"
QEMU_BIN="qemu-system-riscv64"

TMP_IMG="$(mktemp -t xv6-fs.XXXXXX)"
cp "$xv6_DIR/fs.img" "$TMP_IMG"

QEMU_OPTS="-machine virt -bios none -kernel $xv6_DIR/kernel/kernel -m 128M -smp 1 -nographic"
QEMU_OPTS+=" -global virtio-mmio.force-legacy=false"
QEMU_OPTS+=" -drive file=$TMP_IMG,if=none,format=raw,id=x0"
QEMU_OPTS+=" -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"
QEMU_OPTS+=" -netdev user,id=en0"
QEMU_OPTS+=" -device virtio-net-device,netdev=en0,csum=off,gso=off,guest_csum=off,bus=virtio-mmio-bus.1"

echo "=== xv7 vim 測試 ==="

# 0. 清理舊程序
pkill -9 -f "$QEMU_BIN" 2>/dev/null || true
sleep 1

# 1. Build kernel and fs.img
cd "$xv6_DIR"
make TOOLPREFIX=riscv64-unknown-elf- kernel/kernel fs.img

if command -v tmux >/dev/null 2>&1; then
  TMUX_SESSION="xv6-vim-test"
  # 2. Start QEMU in tmux
  if tmux has-session -t "$TMUX_SESSION" 2>/dev/null; then
    tmux kill-session -t "$TMUX_SESSION"
  fi

  TMUX_CMD="$QEMU_BIN $QEMU_OPTS"

  tmux new-session -d -s "$TMUX_SESSION"
  tmux send-keys -t "$TMUX_SESSION" "$TMUX_CMD" C-m

  # 3. Wait for boot
  sleep 6

  # 4. Run vim and edit
  TMUX_TARGET="$TMUX_SESSION"
  tmux send-keys -t "$TMUX_TARGET" "vim test.txt" C-m
  sleep 1
  tmux send-keys -t "$TMUX_TARGET" -l "ihello xv6"
  tmux send-keys -t "$TMUX_TARGET" C-m
  tmux send-keys -t "$TMUX_TARGET" -l "second line"
  sleep 1
  tmux send-keys -t "$TMUX_TARGET" Escape
  sleep 1
  # Test 0/$/x/dd
  tmux send-keys -t "$TMUX_TARGET" -l "0x"
  sleep 1
  tmux send-keys -t "$TMUX_TARGET" -l "j$hx"
  sleep 1
  tmux send-keys -t "$TMUX_TARGET" -l "dd"
  sleep 1
  # Save (Ctrl+S) and quit (Ctrl+Q)
  tmux send-keys -t "$TMUX_TARGET" C-s
  sleep 1
  tmux send-keys -t "$TMUX_TARGET" C-q
  sleep 1
  tmux send-keys -t "$TMUX_TARGET" -l "cat test.txt"
  tmux send-keys -t "$TMUX_TARGET" C-m
  sleep 1

  OUTPUT=$(tmux capture-pane -t "$TMUX_TARGET" -p)

  if echo "$OUTPUT" | grep -q "hello xv6"; then
    echo "PASS: vim saved content"
  else
    echo "FAIL: vim did not save content"
    echo "--- capture ---"
    echo "$OUTPUT" | tail -200
    exit 1
  fi

  echo "=== 完成，請手動關閉 QEMU ==="
  echo "tmux kill-session -t $TMUX_SESSION"
else
  echo "tmux not found, using expect..."
  EXPECT_SCRIPT="$(mktemp -t xv6-vim-test.XXXXXX)"
  cat > "$EXPECT_SCRIPT" <<'EOS'
#!/usr/bin/expect -f
set timeout 60
set qemu_cmd [lindex $argv 0]
spawn {*}$qemu_cmd

# Wait for shell prompt
expect {
  -re "\\$ $" { }
  timeout { puts "FAIL: boot timeout"; exit 1 }
}

send "vim test.txt\r"
# Give vim a moment to draw
sleep 1

send "i"
send "hello xv6"
send "\r"
send "second line"
# ESC
send "\033"
# Test 0/$/x/dd
send "0x"
sleep 1
send "j"
send "\\$"
send "hx"
sleep 1
send "dd"
# Save and quit
send "\023"
sleep 1
send "\021"

# Verify
expect {
  -re "\\$ $" { }
  timeout { }
}
send "cat test.txt\r"

expect {
  -re "second" { puts "FAIL: dd did not remove line2"; exit 1 }
  -re "hello xv6\\r\\n\\$ " { puts "PASS: vim saved content and edits" }
  timeout { puts "FAIL: missing edited line1 or prompt"; exit 1 }
}

# Quit QEMU (Ctrl-A x)
send "\001x"
EOS
  chmod +x "$EXPECT_SCRIPT"
  "$EXPECT_SCRIPT" "$QEMU_BIN $QEMU_OPTS"
  rm -f "$EXPECT_SCRIPT"
fi

rm -f "$TMP_IMG"
