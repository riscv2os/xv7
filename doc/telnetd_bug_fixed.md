# telnetd TCP Panic Bug 修復報告

**日期**: 2026-03-23  
**問題**: telnetd 連線時發生 `panic: acquire`  
**根本原因**: Interrupt-context locking bug

## 問題描述

當 telnetd 服務啟動並接受客戶端連線後，輸入任意命令會觸發 kernel panic：

```
panic: acquire
```

錯誤發生在 `kernel/net/tcp.c` 的 `tcp_input` 函數中。

## 根本原因分析

### 問題根源

這是一個經典的 **interrupt-context locking bug**。

1. telnetd 進程調用 `send()` → `tcp_send()` → `mutex_lock(&tcp_mutex)`
2. 持有 TCP mutex 時，可能觸發網路處理（ARP、ICMP 等）
3. 這些處理可能觸發 software interrupt
4. interrupt handler 調用 `tcp_input()` 嘗試獲取同一個 TCP mutex
5. 由於是同一個 CPU 且已經持有鎖，`holding()` 返回 true
6. 導致 `panic("acquire")`

### 程式碼分析

在 `kernel/proc.c` 的 `sleep()` 函數中：

```c
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  release(lk);  // 釋放 lk 但不釋放 p->lock
  // ...
  sched();
  // ...
  release(&p->lock);
  acquire(lk);  // 重新獲取 lk
}
```

當 `sched_sleep()` 釋放 mutex 時，如果網路 interrupt 剛好發生，就會導致悲劇。

## 修復方案

採用 **try-lock** 機制：interrupt handler 嘗試獲取鎖，如果失敗則跳過這次處理，而不是 blocking。

### 1. 新增 `mutex_trylock()` 函數

**檔案**: `kernel/net/platform/xv6-riscv/platform.h`

```c
static inline int
mutex_trylock(mutex_t *mutex)
{
    push_off();
    if(__sync_lock_test_and_set(&mutex->locked, 1) != 0) {
        pop_off();
        return 0;  // 鎖已被占用
    }
    __sync_synchronize();
    mutex->cpu = mycpu();
    return 1;  // 成功獲取
}
```

### 2. 修改 `tcp_input()`

**檔案**: `kernel/net/tcp.c` (第 747 行)

```c
// 原始程式碼
mutex_lock(&mutex);
tcp_segment_arrives(&seg, hdr->flg, (uint8_t *)hdr + hlen, len - hlen, &local, &foreign);
mutex_unlock(&mutex);

// 修復後
if (!mutex_trylock(&mutex)) {
    return;  // 如果鎖被占用，丟棄這個 packet
}
tcp_segment_arrives(&seg, hdr->flg, (uint8_t *)hdr + hlen, len - hlen, &local, &foreign);
mutex_unlock(&mutex);
```

### 3. 修改 `tcp_timer()`

**檔案**: `kernel/net/tcp.c` (第 758 行)

```c
// 原始程式碼
mutex_lock(&mutex);

// 修復後
if (!mutex_trylock(&mutex)) {
    return;  // 如果鎖被占用，跳過這次 timer 處理
}
```

### 4. 修改 `tcp event_handler()`

**檔案**: `kernel/net/tcp.c` (第 776 行)

```c
// 原始程式碼
mutex_lock(&mutex);

// 修復後
if (!mutex_trylock(&mutex)) {
    return;
}
```

### 5. 修改 `udp_input()`

**檔案**: `kernel/net/udp.c` (第 180 行)

```c
// 原始程式碼
mutex_lock(&mutex);

// 修復後
if (!mutex_trylock(&mutex)) {
    return;
}
```

### 6. 修改 `udp event_handler()`

**檔案**: `kernel/net/udp.c` (第 259 行)

```c
// 原始程式碼
mutex_lock(&mutex);

// 修復後
if (!mutex_trylock(&mutex)) {
    return;
}
```

## 修復原理

使用 try-lock 的優點：
1. **不會阻塞**: interrupt handler 不會因為等待鎖而卡住系統
2. **不會 panic**: 不會重複獲取同一個鎖
3. **優雅降級**: 如果鎖被占用，只是暫時跳過這次處理，下次 timer interrupt 或 packet 到來時會再試

缺點：
- 可能丟失少數 timer tick 或 packet
- 對於 TCP 來說，丢棄的 packet 會因為 TCP 的可靠傳輸機制而重傳

## 測試結果

修復後：
- telnetd 可以正常接受連線
- 輸入命令不再觸發 panic
- httpd 和 curl 功能正常
- 沒有任何 `panic: acquire` 錯誤

## 總結

這是一個在嵌入式作業系統和網路堆疊中常見的並發問題。解決方案是確保 interrupt handler 使用非阻塞的鎖獲取方式，避免與正常程式碼路徑產生競爭。

**修改的檔案**:
- `kernel/net/platform/xv6-riscv/platform.h` - 新增 `mutex_trylock()`
- `kernel/net/tcp.c` - 修改 `tcp_input()`, `tcp_timer()`, `event_handler()`
- `kernel/net/udp.c` - 修改 `udp_input()`, `event_handler()`
