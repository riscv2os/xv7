# xv7 虛擬機 (vm.c) 實作計畫

## 目標

撰寫一支純 C 程式 `vm/vm.c`，在 host 上模擬一台 RISC-V 64 位元虛擬機器，能夠載入 xv7 的 `kernel/kernel` ELF 映像檔並搭配 `fs.img` 磁碟映像，開機至 xv7 shell。功能定位類似一個精簡版 QEMU。

---

## 硬體模型分析

xv7 基於 QEMU `virt` 機器，其啟動參數如下：

```
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M
  -smp 3 -nographic
  -global virtio-mmio.force-legacy=false
  -drive file=fs.img,if=none,format=raw,id=x0
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
  -device virtio-net-device,netdev=en0,bus=virtio-mmio-bus.1
```

### 記憶體映射

| 位址              | 裝置                        |
|-------------------|-----------------------------|
| `0x00101000`      | Goldfish RTC (時鐘)          |
| `0x0C000000`      | PLIC (中斷控制器)            |
| `0x10000000`      | UART 16550a (串列埠)         |
| `0x10001000`      | VIRTIO0 (virtio-blk 磁碟)   |
| `0x10002000`      | VIRTIO1 (virtio-net 網路)    |
| `0x80000000`      | RAM 起始 (kernel 載入點)     |
| `0x80000000+128M` | RAM 結束 (PHYSTOP)           |

### IRQ 編號

| IRQ | 裝置       |
|-----|-----------|
| 1   | VIRTIO0   |
| 2   | VIRTIO1   |
| 10  | UART0     |

### CPU 模型

- RISC-V 64 位元, ISA: RV64G (RV64IMAFDC)
- 特權等級: M-mode → S-mode → U-mode
- 分頁: Sv39 (3-level page table)
- 計時器: `stimecmp` CSR (Sstc 擴展)
- PMP: 至少一個 pmpaddr0/pmpcfg0

---

## 設計方案

### 簡化策略

為了在合理的程式碼量內完成（目標 ~3000-4000 行 C），做以下簡化：

1. **單核 (1 CPU)** — 僅模擬 hart 0，避免多執行緒同步的複雜度
2. **純解譯器** — 不做 JIT，逐指令 fetch-decode-execute
3. **不精確計時** — 用 host clock 驅動 `time` CSR，不按指令計數
4. **不模擬 FPU** — 浮點指令可先略過 (xv7 kernel 不使用浮點)
5. **簡化 virtio-net** — 第一版先不實作網路，回報 device-id=0
6. **不模擬 A 擴展的 LR/SC** — 單核下可用普通 load/store 取代

### 模組架構

```
vm.c 單檔結構:
├── RISC-V CPU 核心
│   ├── 暫存器 (x0-x31, pc)
│   ├── CSR 暫存器 (mstatus, mepc, medeleg, mideleg, sstatus,
│   │    sepc, scause, stval, stvec, satp, sie, sip, stimecmp,
│   │    mhartid, mcounteren, menvcfg, pmpcfg0, pmpaddr0, time)
│   ├── 特權等級切換 (M → S → U)
│   ├── 指令解碼 & 執行 (RV64I 基本指令集 + M 擴展乘除法)
│   ├── 例外與中斷處理 (ecall, ebreak, page fault, timer, external)
│   └── Sv39 MMU (虛擬位址 → 實體位址轉換)
├── 記憶體子系統
│   ├── 128MB RAM (malloc)
│   └── MMIO 分派 (依位址範圍導向各裝置)
├── 裝置模擬
│   ├── UART 16550a (stdin/stdout)
│   ├── PLIC (中斷路由)
│   ├── Goldfish RTC (gettimeofday)
│   ├── VIRTIO-BLK (fs.img 磁碟讀寫)
│   └── VIRTIO-NET (stub: device-id=0 表示不存在)
└── 主程式
    ├── 命令列解析 (-kernel, -drive, -m, -nographic)
    ├── ELF 載入器
    └── 主迴圈 (fetch → decode → execute → check interrupt)
```

---

## 指令集範圍

### 必須實作的指令

**RV64I (整數基本集):**
- 算術: ADD, SUB, ADDI, ADDW, ADDIW, SUBW
- 邏輯: AND, OR, XOR, ANDI, ORI, XORI
- 移位: SLL, SRL, SRA, SLLI, SRLI, SRAI, SLLW, SRLW, SRAW, SLLIW, SRLIW, SRAIW
- 比較: SLT, SLTU, SLTI, SLTIU
- 載入: LB, LH, LW, LD, LBU, LHU, LWU
- 儲存: SB, SH, SW, SD
- 分支: BEQ, BNE, BLT, BGE, BLTU, BGEU
- 跳轉: JAL, JALR
- 上位: LUI, AUIPC
- 系統: ECALL, EBREAK, FENCE
- CSR: CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI
- 特權: MRET, SRET, SFENCE.VMA, WFI

**RV64M (乘除法):**
- MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU
- MULW, DIVW, DIVUW, REMW, REMUW

**RV64A (原子操作 — 單核簡化):**
- LR.W, SC.W, LR.D, SC.D → 簡化為普通 load/store
- AMOSWAP, AMOADD, AMOAND, AMOOR, AMOXOR, AMOMIN, AMOMAX, AMOMINU, AMOMAXU (.W/.D)

### 不實作

- RV64F/D (浮點) — kernel 不使用
- C 壓縮指令 — kernel 以 `-march=rv64g` 編譯，不含 C 擴展

---

## 裝置模擬細節

### UART 16550a
- 暫存器: RHR, THR, IER, FCR, ISR, LCR, LSR (各 1 byte)
- 讀 THR → 從 stdin 讀一個字元
- 寫 THR → 寫到 stdout
- LSR bit0 (RX_READY): 用 `select()/poll()` 探測 stdin 是否有資料
- LSR bit5 (TX_IDLE): 永遠為 1 (stdout 永遠就緒)
- IER 寫入時更新中斷使能，有字元可讀時觸發 IRQ 10

### PLIC
- 32 個 IRQ source, 每個 4 bytes priority
- Per-hart S-mode enable bitmap (offset 0x2080)
- Per-hart S-mode priority threshold (offset 0x201000)
- Per-hart S-mode claim/complete (offset 0x201004)
- claim: 回傳最高優先的 pending IRQ；complete: 標記 IRQ 處理完畢

### Goldfish RTC
- 偏移 0x00: time_low (unix timestamp 低 32 位, 奈秒)
- 偏移 0x04: time_high (高 32 位)
- 用 host 的 `gettimeofday()` 轉換

### VIRTIO-BLK (Modern MMIO, version 2)
- Magic: 0x74726976, Version: 2, Device ID: 2, Vendor: 0x554d4551
- 實作 virtqueue 0: descriptor ring, avail ring, used ring
- 支援 VIRTIO_BLK_T_IN (讀) 和 VIRTIO_BLK_T_OUT (寫)
- 讀寫 `fs.img` 檔案的對應 sector
- queue notify 時處理所有 pending descriptors
- 完成後更新 used ring 並觸發 IRQ 1

### VIRTIO-NET (stub)
- Device ID: 0 (不存在)，讓 kernel 偵測後跳過

---

## 檔案結構

### [NEW] `vm/vm.c`
單一 C 檔案包含完整虛擬機實作，約 3000-4000 行。

### [NEW] `vm/Makefile`
```makefile
CC = gcc
CFLAGS = -Wall -O2
vm: vm.c
	$(CC) $(CFLAGS) -o vm vm.c
clean:
	rm -f vm
```

---

## 開機流程

1. 解析命令列: `./vm/vm -kernel kernel/kernel -drive fs.img`
2. 分配 128MB RAM (malloc)
3. 載入 kernel ELF → 將 LOAD segments 複製到 RAM 的對應實體位址
4. 設定 CPU 初始狀態:
   - `pc = 0x80000000` (kernel entry point)
   - `mhartid = 0`
   - 特權等級 = M-mode
5. 進入主迴圈: fetch → decode → execute → check_interrupts
6. kernel `start()` 設定 CSR、切換到 S-mode via `mret`
7. kernel `main()` 初始化各子系統
8. 啟動 shell → 使用者可透過 terminal 輸入命令

---

## 驗證計畫

### 自動測試

```bash
# 1. 編譯 VM
cd vm && make

# 2. 先在上層目錄用 cross-compiler 編譯 xv7 kernel 和 fs.img
cd .. && make kernel/kernel fs.img

# 3. 用 VM 啟動 xv7
cd vm && ./vm -kernel ../kernel/kernel -drive ../fs.img
```

### 手動驗證

1. **開機訊息**: 應看到 `xv6 kernel is booting` 和日期時間
2. **Shell 互動**: 出現 `$` 提示符後，輸入 `ls` 應列出檔案清單
3. **基本指令**: 輸入 `echo hello` 應輸出 `hello`
4. **Ctrl+P**: 列出行程清單
5. **比對**: 與 `make qemu NET=none` 的行為做對照

---

## 風險與限制

1. **效能**: 純解譯器比 QEMU (JIT) 慢約 100-1000 倍，但對教學目的足夠
2. **網路**: 第一版不支援，如需要可後續加入 TAP/user-mode networking
3. **多核**: 僅支援單核，xv7 的 SMP 部分需設 NCPU=1 或只讓 hart 0 開機
4. **浮點**: 不支援，若 user-space 程式用到浮點會觸發 illegal instruction
5. **除錯**: 暫不提供 GDB stub，可加入 `-d` flag 印出 instruction trace
