debian@debian:/mnt/macos/debian/c0computer/os/os0$ ./nettest.sh
=== xv6-riscv-net 網路自動測試 ===
[0/6] 清理舊的程序...
[1/6] 設定 tap0 網路介面...
[2/6] 清理舊的 session...
[3/6] 啟動 xv6 qemu...
[4/6] 等待 xv6 開機...
[5/6] 設定 xv6 網路...
[6/6] 測試 UDP Echo...
測試訊息


=== 測試結果 ===
14:30:41.413 [D] arp_resolve: resolved, pa=192.0.2.1, ha=56:8f:e7:ec:7f:ac (kern
el/net/arp.c:298)
14:30:41.414 [D] net_device_output: dev=net0, type=0x0800, len=41 (kernel/net/ne
t.c:170)
14:30:41.414 [D] ether_transmit_helper: dev=net0, type=0x0800, len=60 (kernel/ne
t/ether.c:88)
        src: 52:54:00:12:34:56
        dst: 56:8f:e7:ec:7f:ac
       type: 0x0800


=== 清理 ===
按 Ctrl+C 結束測試，完成後執行以下指令關閉 qemu:
  tmux kill-session -t xv6-net-test
debian@debian:/mnt/macos/debian/c0computer/os/os0$ ./httptest.sh
=== xv6-riscv-net HTTP 測試 ===
[0/4] 清理舊的程序...
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