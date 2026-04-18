# xv6-riscv-net

 xv6 OS with TCP/IP stack, runs on QEMU RISC-V.

## Build & Run

```bash
make qemu          # Build kernel + fs.img, run QEMU (requires sudo for tap0)
make qemu NET=user    # User-mode network, no sudo required
make qemu NET=none    # No network
make clean        # Remove build artifacts
```

## Project Structure

- `kernel/` - Kernel code (trap, vm, proc, fs, syscalls)
- `kernel/net/` - TCP/IP stack (ether, ip, arp, icmp, udp, tcp, socket)
- `kernel/net/platform/xv6-riscv/` - virtio-net driver
- `user/` - User programs (sh, cat, grep, tcpecho, udpecho, httpd, telnetd, vim, etc.)
- `mkfs/` - Filesystem image creator

## Important Commands

- `make` auto-detects RISC-V toolchain prefix: `riscv64-unknown-elf-*` or `riscv64-elf-*`
- `COMPILER=gcc` or `COMPILER=clang`
- User programs are baked into `fs.img` via `mkfs/mkfs` - rebuild fs.img after code changes
- QEMU runs with 3 CPUs by default: `make qemu CPUS=1`

## Notes

- Network TAP device `tap0` (192.0.2.1/24 host), guest uses `net0` interface with IP 192.0.2.2
- GDB available: `make qemu-gdb` then `gdb kernel/kernel`
- Uses custom syssocket.c for socket syscall handlers