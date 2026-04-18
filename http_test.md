debian@debian:/mnt/macos/debian/xv6-riscv-net$ ./httptest.sh
=== xv6-riscv-net HTTP 測試 ===
[1/4] 設定 tap0 網路...
[2/4] 清理舊的 session...
[3/4] 啟動 xv6 qemu...
[4/4] 等待開機並執行測試...

=== 啟動 HTTP 測試 ===

=== 測試結果 ===
Starting HTTP Server on port 8080
socket: success, soc=3
waiting for connection...

=== 外部 curl 測試 (從主機) ===
<h1>Hello!</h
=== 清理 ===
完成後執行以下指令關閉:
  tmux kill-session -t xv6-http-test