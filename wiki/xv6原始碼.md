# xv6原始碼

原始的 xv6-riscv 是由 MIT 的 [PDOS 實驗室](https://pdos.csail.mit.edu/) 開發的教學用作業系統。它是針對 RISC-V 多核心處理器重新實作的 Unix v6。

## 程式碼參考

- **儲存庫**：`/Users/Shared/ccc/github/xv6-riscv`
- **官方來源**：https://github.com/mit-pdos/xv6-riscv
- **文件**：[xv6 書籍 (PDF)](https://pdos.csail.mit.edu/6.1810/2025/xv6/book-riscv-rev5.pdf)

## 關鍵檔案

| 檔案 | 說明 |
|------|------|
| `kernel/main.c` | 核心進入點、初始化 |
| `kernel/proc.c` | 行程管理、排程器 |
| `kernel/vm.c` | 虛擬記憶體、頁表管理 |
| `kernel/fs.c` | SFS 檔案系統實作 |
| `kernel/trap.c` | 陷阱與中斷處理 |
| `kernel/syscall.c` | 系統呼叫實作 |

## 架構

xv6 遵循傳統的 Unix 設計：

1. **整合式核心** - 所有 OS 服務都在核心空間
2. **簡單檔案系統** - SFS (Simple File System)
3. **行程排程** - 輪詢式排程
4. **虛擬記憶體** - SV39 頁表（39 位元虛擬位址）

## 書籍章節重點

- **第二章**：作業系統組織、核心位址空間
- **第三章**：行程與虛擬記憶體
- **第四章**：排程與上下文切換
- **第五章**：系統呼叫與陷阱
- **第六章**：裝置驅動程式與中斷
- **第七章**：檔案系統實作
- **第八章**：並發與鎖定

## 參見
- [系統架構](系統架構.md)
- [行程管理](行程管理.md)
- [記憶體管理](記憶體管理.md)
- [檔案系統](檔案系統.md)