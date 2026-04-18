# Virtio網路驅動程式

Virtio 網路驅動程式是 xv7 網路功能的底層基礎，透過 VirtIO 規範與 QEMU 模擬的網路卡進行溝通。

## VirtIO 規範

VirtIO 是一種高效能的虛擬化介面標準，讓客端作業系統可以直接與 Hypervisor 通訊，無需模擬傳統的硬體。

## VirtIO 網路卡

在 QEMU 中，VirtIO 網路卡映射到：
- **MMIO 位址**：0x10002000（VIRTIO1）
- **中斷 IRQ**：2

## VirtIO 佇列 (VirtQueue)

VirtIO 使用三種佇列進行通訊：

```c
struct virtq {
    struct virtq_desc *desc;   // 描述符陣列
    struct virtq_avail *avail;  // 可用區域
    struct virtq_used *used;   // 已使用區域
    int num;                    // 佇列大小
    int last_used_idx;          // 上次使用的索引
    char *free;                 // 閒置描述符
};
```

### 描述符結構

```c
struct virtq_desc {
    uint64 addr;    // 實體位址
    uint32 len;    // 長度
    uint16 flags;  // 標誌 (VRING_DESC_F_NEXT, VRING_DESC_F_WRITE)
    uint16 next;   // 下一個描述符索引
};
```

## 驅動程式運作

### 初始化流程

1. **選擇佇列**：`VIRTIO_MMIO_QUEUE_SEL`
2. **配置佇列記憶體**：配置三個頁面（desc, avail, used）
3. **設定佇列大小**：寫入 `VIRTIO_MMIO_QUEUE_NUM`
4. **啟動佇列**：設定 `VIRTIO_MMIO_QUEUE_READY`

### 封包傳輸

1. 配置傳輸描述符（包含緩衝區位址）
2. 將描述符索引寫入 avail 佇列
3. 通知裝置（透過可用環）
4. 等待傳輸完成中斷
5. 處理 used 佇列中的完成事件

### 封包接收

1. 配置接收描述符（預先配置的緩衝區）
2. 將描述符加入可用佇列
3. 收到中斷後，從 used 佇列取得封包

## 相關檔案

- `kernel/net/platform/xv6-riscv/virtio_net.c` - 主要驅動程式
- `kernel/virtio.h` - VirtIO 暫存器定義
- `kernel/net/ether.c` - 乙太網路層

## 參見
- [網路堆疊](網路堆疊.md)
- [系統架構](系統架構.md)