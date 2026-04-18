/*
 * vm.c — A minimal RISC-V 64-bit virtual machine for xv7
 * Emulates: RV64IMA CPU, UART 16550a, PLIC, Goldfish RTC, VIRTIO-BLK
 * Platforms: macOS / Linux (POSIX)
 *
 * Usage: ./vm -kernel kernel/kernel -drive fs.img
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/select.h>

/* ── types ── */
typedef uint64_t u64;
typedef int64_t  i64;
typedef uint32_t u32;
typedef int32_t  i32;
typedef uint16_t u16;
typedef uint8_t  u8;

/* ── constants ── */
#define RAM_BASE   0x80000000ULL
#define RAM_SIZE   (128*1024*1024ULL)
#define UART_BASE  0x10000000ULL
#define UART_SIZE  8
#define PLIC_BASE  0x0C000000ULL
#define PLIC_SIZE  0x400000ULL
#define VIRTIO0_BASE 0x10001000ULL
#define VIRTIO1_BASE 0x10002000ULL
#define VIRTIO_SIZE  0x1000
#define RTC_BASE   0x00101000ULL
#define RTC_SIZE   8

/* privilege levels */
#define PRV_U 0
#define PRV_S 1
#define PRV_M 3

/* ── UART state ── */
static struct {
    u8 ier, isr, lcr, lsr, mcr;
    u8 rx_buf[256];
    int rx_head, rx_tail;
    bool rx_ready;
} uart;

/* ── PLIC state ── */
static struct {
    u32 priority[32];
    u32 pending;
    u32 senable[8]; /* per-hart s-mode enable */
    u32 spriority[8];
    u32 sclaim[8];
} plic;

/* ── VIRTIO-BLK state ── */
static struct {
    u32 magic, version, device_id, vendor_id;
    u32 device_features, driver_features;
    u32 queue_sel, queue_num, queue_ready;
    u32 status;
    u32 interrupt_status;
    u64 queue_desc, queue_avail, queue_used;
    FILE *disk_fp;
    u64 disk_size;
} vblk;

/* ── CPU state ── */
static struct {
    u64 x[32];
    u64 pc;
    int priv;

    /* M-mode CSRs */
    u64 mstatus, mie, mip;
    u64 medeleg, mideleg;
    u64 mepc, mcause, mtval;
    u64 mtvec;
    u64 mcounteren;
    u64 menvcfg;
    u64 pmpcfg0;
    u64 pmpaddr0;
    u64 mscratch;
    u64 mhartid;

    /* S-mode CSRs */
    u64 sstatus, sie, sip;
    u64 sepc, scause, stval;
    u64 stvec;
    u64 satp;
    u64 sscratch;
    u64 stimecmp;

    /* counters */
    u64 timecmp_host_offset;
    u64 instret;
} cpu;

/* ── Memory ── */
static u8 *ram;

static u64 get_time_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (u64)tv.tv_sec * 1000000000ULL + (u64)tv.tv_usec * 1000ULL;
}

static u64 get_time_csr(void) {
    /* ~10MHz timer */
    return get_time_ns() / 100;
}

/* ── forward declarations ── */
static void plic_update(void);

/* ── UART ── */
static void uart_check_input(void) {
    fd_set fds; struct timeval tv = {0,0};
    FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO+1, &fds, NULL, NULL, &tv) > 0) {
        u8 c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            int next = (uart.rx_tail + 1) % (int)sizeof(uart.rx_buf);
            if (next != uart.rx_head) {
                uart.rx_buf[uart.rx_tail] = c;
                uart.rx_tail = next;
            }
        }
    }
    uart.rx_ready = (uart.rx_head != uart.rx_tail);
}

static u8 uart_read(u64 off) {
    switch (off) {
    case 0: /* RHR */
        if (uart.rx_head != uart.rx_tail) {
            u8 c = uart.rx_buf[uart.rx_head];
            uart.rx_head = (uart.rx_head + 1) % (int)sizeof(uart.rx_buf);
            uart.rx_ready = (uart.rx_head != uart.rx_tail);
            return c;
        }
        return 0;
    case 1: return uart.ier;
    case 2: return (uart.rx_ready && (uart.ier & 1)) ? 0x04 : 0x01; /* ISR */
    case 3: return uart.lcr;
    case 5: /* LSR */
        return (uart.rx_ready ? 0x01 : 0) | 0x20 | 0x40; /* TX always idle */
    default: return 0;
    }
}

static void uart_write(u64 off, u8 val) {
    switch (off) {
    case 0: /* THR */
        if (uart.lcr & 0x80) break; /* baud rate latch mode */
        putchar(val);
        fflush(stdout);
        break;
    case 1: uart.ier = val; break;
    case 2: break; /* FCR write-only */
    case 3: uart.lcr = val; break;
    default: break;
    }
}

static bool uart_has_irq(void) {
    return uart.rx_ready && (uart.ier & 1);
}

/* ── PLIC ── */
static u32 plic_read(u64 off) {
    if (off < 0x80) return plic.priority[off/4];
    if (off >= 0x1000 && off < 0x1004) return plic.pending;
    /* S-mode enable for hart = (off - 0x2080) / 0x100 */
    if (off >= 0x2080 && off < 0x2080 + 8*0x100) {
        int hart = (off - 0x2080) / 0x100;
        return plic.senable[hart];
    }
    /* S-mode priority threshold */
    if (off >= 0x201000 && off < 0x201000 + 8*0x2000) {
        int hart = (off - 0x201000) / 0x2000;
        return plic.spriority[hart];
    }
    /* S-mode claim */
    if (off >= 0x201004 && off < 0x201004 + 8*0x2000) {
        int hart = (off - 0x201004) / 0x2000;
        u32 en = plic.senable[hart];
        u32 pend = plic.pending;
        u32 best = 0;
        for (int i = 1; i < 32; i++) {
            if ((en & (1u<<i)) && (pend & (1u<<i)) && plic.priority[i] > 0) {
                best = i; break;
            }
        }
        if (best) plic.pending &= ~(1u << best);
        plic.sclaim[hart] = best;
        return best;
    }
    return 0;
}

static void plic_write(u64 off, u32 val) {
    if (off < 0x80) { plic.priority[off/4] = val; return; }
    if (off >= 0x2080 && off < 0x2080 + 8*0x100) {
        int hart = (off - 0x2080) / 0x100;
        plic.senable[hart] = val; return;
    }
    if (off >= 0x201000 && off < 0x201000 + 8*0x2000) {
        int hart = (off - 0x201000) / 0x2000;
        plic.spriority[hart] = val; return;
    }
    if (off >= 0x201004 && off < 0x201004 + 8*0x2000) {
        /* complete */
        return;
    }
}

static void plic_set_pending(int irq) {
    plic.pending |= (1u << irq);
}

static bool plic_has_irq(int hart) {
    u32 en = plic.senable[hart];
    u32 pend = plic.pending;
    for (int i = 1; i < 32; i++) {
        if ((en & (1u<<i)) && (pend & (1u<<i)) && plic.priority[i] > 0)
            return true;
    }
    return false;
}

/* ── RTC ── */
static u32 rtc_read(u64 off) {
    u64 ns = get_time_ns();
    if (off == 0) return (u32)(ns);
    if (off == 4) return (u32)(ns >> 32);
    return 0;
}

/* ── VIRTIO-BLK ── */
static void vblk_init(const char *path) {
    vblk.magic = 0x74726976;
    vblk.version = 2;
    vblk.device_id = 2;
    vblk.vendor_id = 0x554d4551;
    vblk.device_features = 0;
    if (path) {
        vblk.disk_fp = fopen(path, "r+b");
        if (!vblk.disk_fp) { perror("open disk"); exit(1); }
        fseek(vblk.disk_fp, 0, SEEK_END);
        vblk.disk_size = ftell(vblk.disk_fp);
    }
}

static u32 vblk_read(u64 off) {
    switch (off) {
    case 0x000: return vblk.magic;
    case 0x004: return vblk.version;
    case 0x008: return vblk.device_id;
    case 0x00c: return vblk.vendor_id;
    case 0x010: return vblk.device_features;
    case 0x034: return 8; /* QUEUE_NUM_MAX */
    case 0x044: return vblk.queue_ready;
    case 0x060: return vblk.interrupt_status;
    case 0x070: return vblk.status;
    default: return 0;
    }
}

/* read guest physical memory */
static u8 ram_read8(u64 addr) {
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE)
        return ram[addr - RAM_BASE];
    return 0;
}
static u16 ram_read16(u64 addr) {
    u16 v = 0;
    for (int i = 0; i < 2; i++) v |= (u16)ram_read8(addr+i) << (i*8);
    return v;
}
static u32 ram_read32(u64 addr) {
    u32 v = 0;
    for (int i = 0; i < 4; i++) v |= (u32)ram_read8(addr+i) << (i*8);
    return v;
}
static u64 ram_read64(u64 addr) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) v |= (u64)ram_read8(addr+i) << (i*8);
    return v;
}
static void ram_write8(u64 addr, u8 v) {
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE)
        ram[addr - RAM_BASE] = v;
}
static void ram_write16(u64 addr, u16 v) {
    for (int i = 0; i < 2; i++) ram_write8(addr+i, (v>>(i*8))&0xff);
}
static void ram_write32(u64 addr, u32 v) {
    for (int i = 0; i < 4; i++) ram_write8(addr+i, (v>>(i*8))&0xff);
}
static void ram_write64(u64 addr, u64 v) {
    for (int i = 0; i < 8; i++) ram_write8(addr+i, (v>>(i*8))&0xff);
}

static void vblk_process(void) {
    if (!vblk.disk_fp || !vblk.queue_ready) return;
    u64 desc_base = vblk.queue_desc;
    u64 avail_base = vblk.queue_avail;
    u64 used_base = vblk.queue_used;

    u16 avail_idx = ram_read16(avail_base + 2);
    u16 used_idx = ram_read16(used_base + 2);
    u16 last_used = used_idx;

    while (last_used != avail_idx) {
        u16 desc_idx = ram_read16(avail_base + 4 + (last_used % 8) * 2);

        /* read first descriptor: virtio_blk_req header */
        u64 d0_addr = ram_read64(desc_base + desc_idx * 16);
        u16 d0_next = ram_read16(desc_base + desc_idx * 16 + 14);

        u32 type = ram_read32(d0_addr);
        u64 sector = ram_read64(d0_addr + 8);

        /* second descriptor: data buffer */
        u64 d1_addr = ram_read64(desc_base + d0_next * 16);
        u32 d1_len  = ram_read32(desc_base + d0_next * 16 + 8);
        u16 d1_next = ram_read16(desc_base + d0_next * 16 + 14);

        /* third descriptor: status byte */
        u64 d2_addr = ram_read64(desc_base + d1_next * 16);

        if (type == 0) { /* read */
            fseek(vblk.disk_fp, sector * 512, SEEK_SET);
            u8 buf[4096];
            u32 toread = d1_len < sizeof(buf) ? d1_len : sizeof(buf);
            size_t n = fread(buf, 1, toread, vblk.disk_fp);
            for (u32 i = 0; i < n; i++) ram_write8(d1_addr + i, buf[i]);
        } else if (type == 1) { /* write */
            fseek(vblk.disk_fp, sector * 512, SEEK_SET);
            u8 buf[4096];
            u32 towrite = d1_len < sizeof(buf) ? d1_len : sizeof(buf);
            for (u32 i = 0; i < towrite; i++) buf[i] = ram_read8(d1_addr + i);
            fwrite(buf, 1, towrite, vblk.disk_fp);
            fflush(vblk.disk_fp);
        }

        /* write status = 0 (success) */
        ram_write8(d2_addr, 0);

        /* update used ring */
        u16 ui = last_used % 8;
        ram_write32(used_base + 4 + ui * 8, desc_idx);
        ram_write32(used_base + 4 + ui * 8 + 4, 0);
        last_used++;
        ram_write16(used_base + 2, last_used);

        vblk.interrupt_status |= 1;
        plic_set_pending(1); /* VIRTIO0_IRQ */
    }
}

static void vblk_write(u64 off, u32 val) {
    switch (off) {
    case 0x020: vblk.driver_features = val; break;
    case 0x030: vblk.queue_sel = val; break;
    case 0x038: vblk.queue_num = val; break;
    case 0x044: vblk.queue_ready = val; break;
    case 0x050: vblk_process(); break; /* QUEUE_NOTIFY */
    case 0x064: vblk.interrupt_status &= ~val; break; /* INT_ACK */
    case 0x070: vblk.status = val; break;
    case 0x080: vblk.queue_desc = (vblk.queue_desc & 0xFFFFFFFF00000000ULL) | val; break;
    case 0x084: vblk.queue_desc = (vblk.queue_desc & 0xFFFFFFFF) | ((u64)val << 32); break;
    case 0x090: vblk.queue_avail = (vblk.queue_avail & 0xFFFFFFFF00000000ULL) | val; break;
    case 0x094: vblk.queue_avail = (vblk.queue_avail & 0xFFFFFFFF) | ((u64)val << 32); break;
    case 0x0a0: vblk.queue_used = (vblk.queue_used & 0xFFFFFFFF00000000ULL) | val; break;
    case 0x0a4: vblk.queue_used = (vblk.queue_used & 0xFFFFFFFF) | ((u64)val << 32); break;
    default: break;
    }
}

/* ── VIRTIO1 stub (no net device) ── */
static u32 vnet_read(u64 off) {
    if (off == 0x000) return 0x74726976; /* magic */
    if (off == 0x004) return 2;          /* version */
    if (off == 0x008) return 0;          /* device_id = 0 → not present */
    return 0;
}

/* ── MMIO dispatch ── */
enum { ACC_OK, ACC_FAULT };

static int mmio_read(u64 addr, u64 *val, int size) {
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
        switch (size) {
        case 1: *val = ram_read8(addr); break;
        case 2: *val = ram_read16(addr); break;
        case 4: *val = ram_read32(addr); break;
        case 8: *val = ram_read64(addr); break;
        }
        return ACC_OK;
    }
    if (addr >= UART_BASE && addr < UART_BASE + UART_SIZE) {
        *val = uart_read(addr - UART_BASE);
        return ACC_OK;
    }
    if (addr >= PLIC_BASE && addr < PLIC_BASE + PLIC_SIZE) {
        *val = plic_read(addr - PLIC_BASE);
        return ACC_OK;
    }
    if (addr >= VIRTIO0_BASE && addr < VIRTIO0_BASE + VIRTIO_SIZE) {
        *val = vblk_read(addr - VIRTIO0_BASE);
        return ACC_OK;
    }
    if (addr >= VIRTIO1_BASE && addr < VIRTIO1_BASE + VIRTIO_SIZE) {
        *val = vnet_read(addr - VIRTIO1_BASE);
        return ACC_OK;
    }
    if (addr >= RTC_BASE && addr < RTC_BASE + RTC_SIZE) {
        if (size == 8) {
            *val = get_time_ns();
        } else {
            *val = rtc_read(addr - RTC_BASE);
        }
        return ACC_OK;
    }
    *val = 0;
    return ACC_OK; /* ignore unmapped reads */
}

static int mmio_write(u64 addr, u64 val, int size) {
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
        switch (size) {
        case 1: ram_write8(addr, val); break;
        case 2: ram_write16(addr, val); break;
        case 4: ram_write32(addr, val); break;
        case 8: ram_write64(addr, val); break;
        }
        return ACC_OK;
    }
    if (addr >= UART_BASE && addr < UART_BASE + UART_SIZE) {
        uart_write(addr - UART_BASE, val);
        return ACC_OK;
    }
    if (addr >= PLIC_BASE && addr < PLIC_BASE + PLIC_SIZE) {
        plic_write(addr - PLIC_BASE, val);
        return ACC_OK;
    }
    if (addr >= VIRTIO0_BASE && addr < VIRTIO0_BASE + VIRTIO_SIZE) {
        vblk_write(addr - VIRTIO0_BASE, val);
        return ACC_OK;
    }
    return ACC_OK; /* ignore unmapped writes */
}

/* ── Sv39 MMU ── */
#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)
#define PGSIZE 4096
#define PGSHIFT 12

enum { MMU_READ, MMU_WRITE, MMU_EXEC };

static int mmu_translate(u64 vaddr, u64 *paddr, int access_type) {
    if ((cpu.satp >> 60) != 8) { /* paging disabled */
        *paddr = vaddr;
        return ACC_OK;
    }
    u64 pt_base = (cpu.satp & 0xFFFFFFFFFFFULL) << 12;
    u64 a = pt_base;
    u64 vpn[3];
    vpn[2] = (vaddr >> 30) & 0x1FF;
    vpn[1] = (vaddr >> 21) & 0x1FF;
    vpn[0] = (vaddr >> 12) & 0x1FF;

    u64 pte = 0;
    int i;
    for (i = 2; i >= 0; i--) {
        u64 pte_addr = a + vpn[i] * 8;
        mmio_read(pte_addr, &pte, 8);
        if (!(pte & PTE_V)) return ACC_FAULT;
        if ((pte & PTE_R) || (pte & PTE_X)) break; /* leaf */
        a = ((pte >> 10) & 0xFFFFFFFFFFFULL) << 12;
    }
    if (i < 0) return ACC_FAULT;

    /* check permissions */
    if (access_type == MMU_READ  && !(pte & PTE_R)) return ACC_FAULT;
    if (access_type == MMU_WRITE && !(pte & PTE_W)) return ACC_FAULT;
    if (access_type == MMU_EXEC  && !(pte & PTE_X)) return ACC_FAULT;

    /* check user bit */
    if (cpu.priv == PRV_U && !(pte & PTE_U)) return ACC_FAULT;
    if (cpu.priv == PRV_S && (pte & PTE_U)) {
        /* S-mode cannot access user pages unless SUM is set */
        if (access_type != MMU_EXEC && !(cpu.mstatus & (1ULL << 18)))
            return ACC_FAULT;
        if (access_type == MMU_EXEC) return ACC_FAULT;
    }

    u64 ppn = (pte >> 10) & 0xFFFFFFFFFFFULL;
    u64 pg_offset = vaddr & 0xFFF;
    if (i == 2) {
        /* 1GiB superpage */
        *paddr = (ppn << 12) | (vaddr & 0x3FFFFFFF);
    } else if (i == 1) {
        /* 2MiB megapage */
        *paddr = (ppn << 12) | (vaddr & 0x1FFFFF);
    } else {
        *paddr = (ppn << 12) | pg_offset;
    }

    /* set A/D bits */
    u64 new_pte = pte | PTE_A;
    if (access_type == MMU_WRITE) new_pte |= PTE_D;
    if (new_pte != pte) {
        u64 pte_addr = a + vpn[i < 0 ? 0 : 0] * 8;
        /* recalculate pte_addr properly */
        u64 a2 = pt_base;
        for (int j = 2; j > i; j--) {
            u64 tmp;
            mmio_read(a2 + vpn[j]*8, &tmp, 8);
            a2 = ((tmp >> 10) & 0xFFFFFFFFFFFULL) << 12;
        }
        pte_addr = a2 + vpn[i] * 8;
        mmio_write(pte_addr, new_pte, 8);
    }

    return ACC_OK;
}

/* virtual memory access */
static int vm_read(u64 vaddr, u64 *val, int size) {
    u64 paddr;
    /* M-mode bypasses MMU */
    if (cpu.priv == PRV_M) { paddr = vaddr; }
    else { if (mmu_translate(vaddr, &paddr, MMU_READ)) return ACC_FAULT; }
    return mmio_read(paddr, val, size);
}

static int vm_write(u64 vaddr, u64 val, int size) {
    u64 paddr;
    if (cpu.priv == PRV_M) { paddr = vaddr; }
    else { if (mmu_translate(vaddr, &paddr, MMU_WRITE)) return ACC_FAULT; }
    return mmio_write(paddr, val, size);
}

static int vm_fetch(u64 vaddr, u32 *inst) {
    u64 paddr;
    if (cpu.priv == PRV_M) { paddr = vaddr; }
    else { if (mmu_translate(vaddr, &paddr, MMU_EXEC)) return ACC_FAULT; }
    u64 v;
    int r = mmio_read(paddr, &v, 4);
    *inst = (u32)v;
    return r;
}

/* ── CSR read/write ── */
#define CSR_MSTATUS    0x300
#define CSR_MISA       0x301
#define CSR_MEDELEG    0x302
#define CSR_MIDELEG    0x303
#define CSR_MIE        0x304
#define CSR_MTVEC      0x305
#define CSR_MCOUNTEREN 0x306
#define CSR_MENVCFG    0x30A
#define CSR_MSCRATCH   0x340
#define CSR_MEPC       0x341
#define CSR_MCAUSE     0x342
#define CSR_MTVAL      0x343
#define CSR_MIP        0x344
#define CSR_PMPCFG0    0x3A0
#define CSR_PMPADDR0   0x3B0
#define CSR_MHARTID    0xF14
#define CSR_SSTATUS    0x100
#define CSR_SIE        0x104
#define CSR_STVEC      0x105
#define CSR_SSCRATCH   0x140
#define CSR_SEPC       0x141
#define CSR_SCAUSE     0x142
#define CSR_STVAL      0x143
#define CSR_SIP        0x144
#define CSR_SATP       0x180
#define CSR_STIMECMP   0x14D
#define CSR_TIME       0xC01
#define CSR_CYCLE      0xC00
#define CSR_INSTRET    0xC02

/* sstatus is a view of mstatus */
#define SSTATUS_MASK 0x800DE162ULL

static u64 csr_read(u32 csr) {
    switch (csr) {
    case CSR_MSTATUS:    return cpu.mstatus;
    case CSR_MISA:       return (2ULL << 62) | 0x141101; /* RV64IMASU */
    case CSR_MEDELEG:    return cpu.medeleg;
    case CSR_MIDELEG:    return cpu.mideleg;
    case CSR_MIE:        return cpu.mie;
    case CSR_MTVEC:      return cpu.mtvec;
    case CSR_MCOUNTEREN: return cpu.mcounteren;
    case CSR_MENVCFG:    return cpu.menvcfg;
    case CSR_MSCRATCH:   return cpu.mscratch;
    case CSR_MEPC:       return cpu.mepc;
    case CSR_MCAUSE:     return cpu.mcause;
    case CSR_MTVAL:      return cpu.mtval;
    case CSR_MIP:        return cpu.mip;
    case CSR_PMPCFG0:    return cpu.pmpcfg0;
    case CSR_PMPADDR0:   return cpu.pmpaddr0;
    case CSR_MHARTID:    return cpu.mhartid;
    case CSR_SSTATUS:    return cpu.mstatus & SSTATUS_MASK;
    case CSR_SIE:        return cpu.mie & cpu.mideleg;
    case CSR_STVEC:      return cpu.stvec;
    case CSR_SSCRATCH:   return cpu.sscratch;
    case CSR_SEPC:       return cpu.sepc;
    case CSR_SCAUSE:     return cpu.scause;
    case CSR_STVAL:      return cpu.stval;
    case CSR_SIP:        return cpu.mip & cpu.mideleg;
    case CSR_SATP:       return cpu.satp;
    case CSR_STIMECMP:   return cpu.stimecmp;
    case CSR_TIME: case CSR_CYCLE: return get_time_csr();
    case CSR_INSTRET:    return cpu.instret;
    default: return 0;
    }
}

static void csr_write(u32 csr, u64 val) {
    switch (csr) {
    case CSR_MSTATUS:    cpu.mstatus = val; break;
    case CSR_MEDELEG:    cpu.medeleg = val; break;
    case CSR_MIDELEG:    cpu.mideleg = val; break;
    case CSR_MIE:        cpu.mie = val; break;
    case CSR_MTVEC:      cpu.mtvec = val; break;
    case CSR_MCOUNTEREN: cpu.mcounteren = val; break;
    case CSR_MENVCFG:    cpu.menvcfg = val; break;
    case CSR_MSCRATCH:   cpu.mscratch = val; break;
    case CSR_MEPC:       cpu.mepc = val; break;
    case CSR_MCAUSE:     cpu.mcause = val; break;
    case CSR_MTVAL:      cpu.mtval = val; break;
    case CSR_MIP:        cpu.mip = val; break;
    case CSR_PMPCFG0:    cpu.pmpcfg0 = val; break;
    case CSR_PMPADDR0:   cpu.pmpaddr0 = val; break;
    case CSR_SSTATUS:    cpu.mstatus = (cpu.mstatus & ~SSTATUS_MASK) | (val & SSTATUS_MASK); break;
    case CSR_SIE:        cpu.mie = (cpu.mie & ~cpu.mideleg) | (val & cpu.mideleg); break;
    case CSR_STVEC:      cpu.stvec = val; break;
    case CSR_SSCRATCH:   cpu.sscratch = val; break;
    case CSR_SEPC:       cpu.sepc = val; break;
    case CSR_SCAUSE:     cpu.scause = val; break;
    case CSR_STVAL:      cpu.stval = val; break;
    case CSR_SIP:        cpu.mip = (cpu.mip & ~cpu.mideleg) | (val & cpu.mideleg); break;
    case CSR_SATP:       cpu.satp = val; break;
    case CSR_STIMECMP:   cpu.stimecmp = val; break;
    default: break;
    }
}

/* ── Trap handling ── */
static void trap(u64 cause, u64 tval) {
    bool is_interrupt = (cause >> 63) & 1;
    u64 exc_code = cause & 0x7FFFFFFFFFFFFFFFULL;
    bool delegate = false;

    if (cpu.priv <= PRV_S) {
        if (is_interrupt) delegate = (cpu.mideleg >> exc_code) & 1;
        else delegate = (cpu.medeleg >> exc_code) & 1;
    }

    if (delegate) {
        /* trap to S-mode */
        cpu.sepc = cpu.pc;
        cpu.scause = cause;
        cpu.stval = tval;
        u64 s = cpu.mstatus;
        /* set SPIE = SIE */
        if (s & (1ULL << 1)) s |= (1ULL << 5); else s &= ~(1ULL << 5);
        /* set SPP = priv */
        if (cpu.priv == PRV_S) s |= (1ULL << 8); else s &= ~(1ULL << 8);
        /* clear SIE */
        s &= ~(1ULL << 1);
        cpu.mstatus = s;
        cpu.priv = PRV_S;
        cpu.pc = cpu.stvec & ~3ULL;
    } else {
        /* trap to M-mode */
        cpu.mepc = cpu.pc;
        cpu.mcause = cause;
        cpu.mtval = tval;
        u64 s = cpu.mstatus;
        /* set MPIE = MIE */
        if (s & (1ULL << 3)) s |= (1ULL << 7); else s &= ~(1ULL << 7);
        /* set MPP = priv */
        s = (s & ~(3ULL << 11)) | ((u64)cpu.priv << 11);
        /* clear MIE */
        s &= ~(1ULL << 3);
        cpu.mstatus = s;
        cpu.priv = PRV_M;
        cpu.pc = cpu.mtvec & ~3ULL;
    }
}

/* ── check pending interrupts ── */
static void check_interrupts(void) {
    /* update timer interrupt */
    u64 t = get_time_csr();
    if (t >= cpu.stimecmp)
        cpu.mip |= (1ULL << 5); /* STIP */
    else
        cpu.mip &= ~(1ULL << 5);

    /* update external interrupt from PLIC */
    if (plic_has_irq(0))
        cpu.mip |= (1ULL << 9); /* SEIP */
    else
        cpu.mip &= ~(1ULL << 9);

    u64 pending = cpu.mip & cpu.mie;
    if (!pending) return;

    /* Check if interrupts are enabled at current privilege level */
    bool m_ie = (cpu.mstatus >> 3) & 1;
    bool s_ie = (cpu.mstatus >> 1) & 1;

    /* try M-mode interrupts (not delegated) */
    u64 m_pending = pending & ~cpu.mideleg;
    if (m_pending && (cpu.priv < PRV_M || (cpu.priv == PRV_M && m_ie))) {
        /* pick highest priority: MEI > MSI > MTI > SEI > SSI > STI */
        int prio[] = {11, 3, 7, 9, 1, 5};
        for (int i = 0; i < 6; i++) {
            if (m_pending & (1ULL << prio[i])) {
                trap((1ULL << 63) | prio[i], 0);
                return;
            }
        }
    }

    /* try S-mode interrupts (delegated) */
    u64 s_pending = pending & cpu.mideleg;
    if (s_pending && (cpu.priv < PRV_S || (cpu.priv == PRV_S && s_ie))) {
        int prio[] = {9, 1, 5}; /* SEIP, SSIP, STIP */
        for (int i = 0; i < 3; i++) {
            if (s_pending & (1ULL << prio[i])) {
                trap((1ULL << 63) | prio[i], 0);
                return;
            }
        }
    }
}

/* ── sign extend helper ── */
static inline i64 sext(u64 v, int bits) {
    u64 m = 1ULL << (bits - 1);
    return (i64)((v ^ m) - m);
}

/* ── CPU execute one instruction ── */
static bool cpu_step(void) {
    u32 inst;
    if (vm_fetch(cpu.pc, &inst)) {
        trap(12, cpu.pc); /* instruction page fault */
        return true;
    }

    u32 opcode = inst & 0x7F;
    u32 rd  = (inst >> 7) & 0x1F;
    u32 rs1 = (inst >> 15) & 0x1F;
    u32 rs2 = (inst >> 20) & 0x1F;
    u32 funct3 = (inst >> 12) & 0x7;
    u32 funct7 = (inst >> 25) & 0x7F;
    i64 imm_i = sext(inst >> 20, 12);
    i64 imm_s = sext(((inst >> 25) << 5) | ((inst >> 7) & 0x1F), 12);
    i64 imm_b = sext(((inst >> 31) << 12) | (((inst >> 7) & 1) << 11) |
                      (((inst >> 25) & 0x3F) << 5) | (((inst >> 8) & 0xF) << 1), 13);
    i64 imm_u = sext((u64)(inst & 0xFFFFF000), 32);
    i64 imm_j = sext(((inst >> 31) << 20) | (((inst >> 12) & 0xFF) << 12) |
                      (((inst >> 20) & 1) << 11) | (((inst >> 21) & 0x3FF) << 1), 21);

    u64 next_pc = cpu.pc + 4;
    u64 r1 = cpu.x[rs1], r2 = cpu.x[rs2];

    #define WR(r,v) do { if (r) cpu.x[r] = (v); } while(0)

    switch (opcode) {
    case 0x37: /* LUI */
        WR(rd, (u64)imm_u); break;
    case 0x17: /* AUIPC */
        WR(rd, cpu.pc + (u64)imm_u); break;
    case 0x6F: /* JAL */
        WR(rd, next_pc);
        next_pc = cpu.pc + imm_j; break;
    case 0x67: /* JALR */
        WR(rd, next_pc);
        next_pc = (r1 + imm_i) & ~1ULL; break;

    case 0x63: /* BRANCH */
        switch (funct3) {
        case 0: if ((i64)r1 == (i64)r2) next_pc = cpu.pc + imm_b; break;
        case 1: if ((i64)r1 != (i64)r2) next_pc = cpu.pc + imm_b; break;
        case 4: if ((i64)r1 <  (i64)r2) next_pc = cpu.pc + imm_b; break;
        case 5: if ((i64)r1 >= (i64)r2) next_pc = cpu.pc + imm_b; break;
        case 6: if (r1 < r2) next_pc = cpu.pc + imm_b; break;
        case 7: if (r1 >= r2) next_pc = cpu.pc + imm_b; break;
        default: trap(2, inst); return true;
        } break;

    case 0x03: { /* LOAD */
        u64 addr = r1 + imm_i, val;
        int sz = (funct3 & 3) == 0 ? 1 : (funct3 & 3) == 1 ? 2 : (funct3 & 3) == 2 ? 4 : 8;
        if (vm_read(addr, &val, sz)) { trap(13, addr); return true; }
        switch (funct3) {
        case 0: val = (i64)(int8_t)val; break;
        case 1: val = (i64)(int16_t)val; break;
        case 2: val = (i64)(int32_t)val; break;
        case 3: break;
        case 4: val &= 0xFF; break;
        case 5: val &= 0xFFFF; break;
        case 6: val &= 0xFFFFFFFF; break;
        }
        WR(rd, val);
    } break;

    case 0x23: { /* STORE */
        u64 addr = r1 + imm_s;
        int sz = funct3 == 0 ? 1 : funct3 == 1 ? 2 : funct3 == 2 ? 4 : 8;
        if (vm_write(addr, r2, sz)) { trap(15, addr); return true; }
    } break;

    case 0x13: /* OP-IMM */
        switch (funct3) {
        case 0: WR(rd, r1 + imm_i); break;
        case 1: WR(rd, r1 << (imm_i & 0x3F)); break;
        case 2: WR(rd, (i64)r1 < imm_i ? 1 : 0); break;
        case 3: WR(rd, r1 < (u64)imm_i ? 1 : 0); break;
        case 4: WR(rd, r1 ^ (u64)imm_i); break;
        case 5:
            if (funct7 & 0x20) WR(rd, (u64)((i64)r1 >> (imm_i & 0x3F)));
            else WR(rd, r1 >> (imm_i & 0x3F));
            break;
        case 6: WR(rd, r1 | (u64)imm_i); break;
        case 7: WR(rd, r1 & (u64)imm_i); break;
        } break;

    case 0x33: /* OP */
        if (funct7 == 1) { /* M extension */
            i64 sr1 = (i64)r1, sr2 = (i64)r2;
            switch (funct3) {
            case 0: WR(rd, (u64)(sr1 * sr2)); break; /* MUL */
            case 1: { /* MULH */
                __int128 r = (__int128)sr1 * (__int128)sr2;
                WR(rd, (u64)(r >> 64)); } break;
            case 2: { /* MULHSU */
                __int128 r = (__int128)sr1 * (unsigned __int128)r2;
                WR(rd, (u64)(r >> 64)); } break;
            case 3: { /* MULHU */
                unsigned __int128 r = (unsigned __int128)r1 * (unsigned __int128)r2;
                WR(rd, (u64)(r >> 64)); } break;
            case 4: WR(rd, sr2 ? (u64)(sr1 / sr2) : ~0ULL); break;
            case 5: WR(rd, r2 ? r1 / r2 : ~0ULL); break;
            case 6: WR(rd, sr2 ? (u64)(sr1 % sr2) : (u64)sr1); break;
            case 7: WR(rd, r2 ? r1 % r2 : r1); break;
            }
        } else {
            switch (funct3) {
            case 0: WR(rd, funct7 & 0x20 ? r1 - r2 : r1 + r2); break;
            case 1: WR(rd, r1 << (r2 & 0x3F)); break;
            case 2: WR(rd, (i64)r1 < (i64)r2 ? 1 : 0); break;
            case 3: WR(rd, r1 < r2 ? 1 : 0); break;
            case 4: WR(rd, r1 ^ r2); break;
            case 5: WR(rd, funct7 & 0x20 ? (u64)((i64)r1 >> (r2&0x3F)) : r1 >> (r2&0x3F)); break;
            case 6: WR(rd, r1 | r2); break;
            case 7: WR(rd, r1 & r2); break;
            }
        } break;

    case 0x1B: /* OP-IMM-32 */
        switch (funct3) {
        case 0: WR(rd, (i64)(i32)(r1 + imm_i)); break;
        case 1: WR(rd, (i64)(i32)((u32)r1 << (imm_i & 0x1F))); break;
        case 5:
            if (funct7 & 0x20) WR(rd, (i64)(i32)((i32)r1 >> (imm_i & 0x1F)));
            else WR(rd, (i64)(i32)((u32)r1 >> (imm_i & 0x1F)));
            break;
        default: trap(2, inst); return true;
        } break;

    case 0x3B: /* OP-32 */
        if (funct7 == 1) { /* M extension 32-bit */
            i32 s1 = (i32)r1, s2 = (i32)r2;
            switch (funct3) {
            case 0: WR(rd, (i64)(i32)(s1 * s2)); break;
            case 4: WR(rd, s2 ? (i64)(i32)(s1 / s2) : (i64)(i32)(~0U)); break;
            case 5: WR(rd, (u32)r2 ? (i64)(i32)((u32)r1 / (u32)r2) : (i64)(i32)(~0U)); break;
            case 6: WR(rd, s2 ? (i64)(i32)(s1 % s2) : (i64)s1); break;
            case 7: WR(rd, (u32)r2 ? (i64)(i32)((u32)r1 % (u32)r2) : (i64)(i32)(u32)r1); break;
            default: trap(2, inst); return true;
            }
        } else {
            switch (funct3) {
            case 0: WR(rd, (i64)(i32)(funct7 & 0x20 ? (u32)r1-(u32)r2 : (u32)r1+(u32)r2)); break;
            case 1: WR(rd, (i64)(i32)((u32)r1 << (r2 & 0x1F))); break;
            case 5:
                if (funct7 & 0x20) WR(rd, (i64)(i32)((i32)r1 >> (r2&0x1F)));
                else WR(rd, (i64)(i32)((u32)r1 >> (r2&0x1F)));
                break;
            default: trap(2, inst); return true;
            }
        } break;

    case 0x0F: /* FENCE */
        break;

    case 0x73: { /* SYSTEM */
        if (inst == 0x00000073) { /* ECALL */
            u64 cause = cpu.priv == PRV_U ? 8 : cpu.priv == PRV_S ? 9 : 11;
            trap(cause, 0); return true;
        }
        if (inst == 0x00100073) { /* EBREAK */
            trap(3, cpu.pc); return true;
        }
        if (inst == 0x30200073) { /* MRET */
            u64 s = cpu.mstatus;
            int mpp = (s >> 11) & 3;
            /* MIE = MPIE */
            if (s & (1ULL << 7)) s |= (1ULL << 3); else s &= ~(1ULL << 3);
            s |= (1ULL << 7); /* MPIE = 1 */
            s &= ~(3ULL << 11); /* MPP = U */
            cpu.mstatus = s;
            cpu.priv = mpp;
            next_pc = cpu.mepc;
            break;
        }
        if (inst == 0x10200073) { /* SRET */
            u64 s = cpu.mstatus;
            int spp = (s >> 8) & 1;
            if (s & (1ULL << 5)) s |= (1ULL << 1); else s &= ~(1ULL << 1);
            s |= (1ULL << 5); /* SPIE = 1 */
            s &= ~(1ULL << 8); /* SPP = U */
            cpu.mstatus = s;
            cpu.priv = spp ? PRV_S : PRV_U;
            next_pc = cpu.sepc;
            break;
        }
        if (inst == 0x10500073 || inst == 0x12000073) { /* WFI / SFENCE.VMA */
            break;
        }
        /* CSR instructions */
        u32 csraddr = (inst >> 20) & 0xFFF;
        u64 old = csr_read(csraddr);
        u64 src = (funct3 & 4) ? rs1 : r1; /* imm or reg */
        u64 nv = old;
        switch (funct3 & 3) {
        case 1: nv = src; break;           /* CSRRW */
        case 2: nv = old | src; break;     /* CSRRS */
        case 3: nv = old & ~src; break;    /* CSRRC */
        }
        if (funct3 & 3) {
            if ((funct3 & 3) == 1 || rs1 != 0) csr_write(csraddr, nv);
            WR(rd, old);
        }
    } break;

    case 0x2F: { /* AMO (A extension) */
        u64 addr = r1;
        int w = (funct3 == 2); /* word(2) or dword(3) */
        int sz = w ? 4 : 8;
        u64 val;
        if (vm_read(addr, &val, sz)) { trap(13, addr); return true; }
        if (w) val = (i64)(i32)(u32)val;
        u32 op = funct7 >> 2;
        if (op == 0x02) { /* LR */
            WR(rd, val); goto amo_done;
        }
        if (op == 0x03) { /* SC */
            if (vm_write(addr, r2, sz)) { trap(15, addr); return true; }
            WR(rd, 0); goto amo_done;
        }
        /* compute new value */
        u64 nv;
        switch (op) {
        case 0x00: nv = val + r2; break;
        case 0x01: nv = r2; break;
        case 0x04: nv = val ^ r2; break;
        case 0x08: nv = val | r2; break;
        case 0x0C: nv = val & r2; break;
        case 0x10: nv = ((i64)val < (i64)r2) ? val : r2; break;
        case 0x14: nv = ((i64)val > (i64)r2) ? val : r2; break;
        case 0x18: nv = (val < r2) ? val : r2; break;
        case 0x1C: nv = (val > r2) ? val : r2; break;
        default: nv = val; break;
        }
        WR(rd, val);
        if (vm_write(addr, w ? (u32)nv : nv, sz)) { trap(15, addr); return true; }
    amo_done:;
    } break;

    default:
        trap(2, inst); return true; /* illegal instruction */
    }

    cpu.pc = next_pc;
    cpu.x[0] = 0;
    cpu.instret++;
    return true;
}

/* ── ELF loader ── */
static void load_elf(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("open kernel"); exit(1); }

    u8 ehdr[64];
    if (fread(ehdr, 1, 64, fp) != 64) { fprintf(stderr, "bad ELF\n"); exit(1); }
    if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F') {
        fprintf(stderr, "not ELF\n"); exit(1);
    }

    u64 phoff    = *(u64*)(ehdr + 32);
    u16 phentsize = *(u16*)(ehdr + 54);
    u16 phnum    = *(u16*)(ehdr + 56);

    for (int i = 0; i < phnum; i++) {
        u8 ph[56];
        fseek(fp, phoff + i * phentsize, SEEK_SET);
        if (fread(ph, 1, 56, fp) != 56) break;
        u32 type = *(u32*)(ph + 0);
        if (type != 1) continue; /* PT_LOAD */
        u64 offset = *(u64*)(ph + 8);
        u64 paddr  = *(u64*)(ph + 24);
        u64 filesz = *(u64*)(ph + 32);
        u64 memsz  = *(u64*)(ph + 40);

        if (paddr >= RAM_BASE && paddr + memsz <= RAM_BASE + RAM_SIZE) {
            fseek(fp, offset, SEEK_SET);
            fread(ram + (paddr - RAM_BASE), 1, filesz, fp);
            if (memsz > filesz)
                memset(ram + (paddr - RAM_BASE) + filesz, 0, memsz - filesz);
        }
    }
    fclose(fp);
}

/* ── terminal raw mode ── */
static struct termios orig_termios;
static bool raw_mode = false;

static void disable_raw_mode(void) {
    if (raw_mode) tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode = true;
}

/* ── main ── */
int main(int argc, char **argv) {
    const char *kernel_path = NULL;
    const char *disk_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-kernel") && i+1 < argc) kernel_path = argv[++i];
        else if (!strcmp(argv[i], "-drive") && i+1 < argc) disk_path = argv[++i];
    }
    if (!kernel_path) {
        fprintf(stderr, "Usage: %s -kernel <kernel-elf> [-drive <disk.img>]\n", argv[0]);
        return 1;
    }

    ram = calloc(1, RAM_SIZE);
    if (!ram) { perror("malloc"); return 1; }

    load_elf(kernel_path);
    if (disk_path) vblk_init(disk_path);

    /* initial CPU state: M-mode, pc = 0x80000000 */
    cpu.pc = RAM_BASE;
    cpu.priv = PRV_M;
    cpu.mhartid = 0;

    enable_raw_mode();

    u64 poll_counter = 0;
    while (1) {
        if (++poll_counter % 1024 == 0) {
            uart_check_input();
            if (uart_has_irq()) plic_set_pending(10);
        }
        check_interrupts();
        if (!cpu_step()) break;
    }

    free(ram);
    return 0;
}
