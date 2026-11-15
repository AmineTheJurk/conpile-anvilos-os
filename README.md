# AnvilOS — Unix-like x86 Operating System

AnvilOS is a real, bare-metal 32-bit x86 Unix-like operating system written from scratch in freestanding C and NASM x86 assembly.

## Key Components
- **Two-Stage USB/BIOS Bootloader (`boot/mbr.asm`, `boot/stage2.asm`)**:
  - Stage 1 MBR with active partition table (`LBA 512`, `8 MiB` image), INT 13h LBA + CHS fallback.
  - Stage 2 enables A20, detects E820 physical RAM, configures VESA VBE 32-bit Linear Framebuffer (`1024x768x32` with `800x600`/`640x480` fallback), loads the kernel and AnvilFS snapshot, and enters 32-bit Protected Mode.
- **32-bit Kernel (`kernel/`)**:
  - GDT, IDT, 8259 PIC remapping, CPU exception handling, `INT 0x80` Unix-like syscall interface.
  - Physical Page Frame Allocator (PMM), 32-bit Virtual Paging (VMM) with VBE LFB + PCI BAR MMIO mapping, and coalescing `kmalloc`/`kfree` kernel heap.
  - Dynamic Loadable Kernel Module loader (`AKM1` `.mod` format with `R_386_32` relocation & `AMIX` fast index).
  - Task registry & custom `.anv` executable loader.
- **Loadable OpenBSD Driver Modules (`modules/`, compiled as `.mod` in `/lib/modules/`)**:
  - **91 standalone OpenBSD hardware modules** covering **2,556 real PCI Vendor:Device IDs** extracted from `github.com/openbsd/src`:
    - **GPU / DRM (`modules/gpu/`)**: `inteldrm` (425 Intel iGPU & Arc IDs), `amdgpu` (312 AMD APU/RDNA IDs), `radeondrm` (699 ATI/AMD Radeon IDs), `viogpu` (VirtIO GPU).
    - **Framebuffer (`modules/fb/`)**: `vesafb`, `efifb`, `vgafb` (OpenBSD `vga_pci` / Bochs / VirtualBox / VMware SVGA / QXL / Cirrus).
    - **Wi-Fi (`modules/wifi/`)**: All 22 OpenBSD PCI wireless drivers (`iwx`, `iwm`, `iwn`, `wpi`, `iwi`, `ipw`, `athn`, `ath`, `bwfm`, `bwi`, `qwx`, `qwz`, `mwx`, `ral`, `rtwn`, `rtw`, `acx`, `atw`, `malo`, `pgt`, `an`, `wi`).
    - **Ethernet (`modules/net/`)**: All 62 OpenBSD PCI/PV Ethernet drivers (`em`, `igc`, `ix`, `ixl`, `ice`, `iavf`, `ixgb`, `fxp`, `re`, `rge`, `rl`, `bge`, `bnx`, `bnxt`, `bce`, `aq`, `alc`, `ale`, `age`, `msk`, `sk`, `mcx`, `nfe`, `pcn`, `ne`, `vio`, `vmx`, `vic`, `vr`, `xl`, and more).
  - **On-Demand Selective Loading**: None of these 91 drivers are baked into `kernel.bin`. At boot, the kernel scans PCI, checks `/lib/modules/modules.idx`, and loads **only** the `.mod` binaries matching the actual detected hardware (e.g., an all-Intel machine loads only `inteldrm.mod`, `iwx.mod`, `em.mod`, `vesafb.mod`).
  - **Incompatible Hardware Prompt**: If any device lacks a compatible `.mod`, AnvilOS displays **"No Compatible Devices On Your Computer! :"**, lists the unmatched devices, and lets you navigate **`[ Continue ]`** / **`[ Shut Down ]`** with the arrow keys (Continue boots anyway; Shut Down powers off via ACPI FADT `_S5_` / hypervisor ports).
- **Filesystem (`fs/anvilfs.c`, `fs/vfs.c`)**:
  - **AnvilFS**: 512-inode Unix-like filesystem (`ANVF` superblock, bitmap, direct + indirect blocks). Mounts persistent **ATA PIO** disk when present, or falls back to **Live USB RAM-disk**.
- **Desktop GUI & Unix Shell (`gui/`, `kernel/wifi.c`, `drivers/rtc.c`)**:
  - **Taskbar & Wi-Fi Chooser**: Custom **AnvilOS** taskbar Start icon, **Wi-Fi icon** next to CMOS RTC **Date & Time** opening an interactive Wi-Fi network chooser (Scan, SSID selection, WPA2 passphrase entry, Connect/Disconnect, `/etc/wifi.conf`), plus `wifi` CLI command.
  - **Desktop Icons & Apps**: **Terminal**, **Files**, **System Info**, **AnvilPad**, **hello.anv**, **sysinfo.anv**, and `ash` shell (`ls`, `lsmod`, `modprobe`, `rmmod`, `ifconfig`, `wifi`, `date`, `lspci`, `df`, `ps`, `free`, `cat`, `write`, `run`, `reboot`, `shutdown`).
- **Custom Executable Format (`.anv`) & `agcc` Compiler (`compiler/agcc.c`)**:
  - Portable C11 compiler producing PIC x86-32 `ANV1` binaries (`agcc` for Linux, `agcc.exe` for Windows x64).

## Flashing to a USB Stick (PassMark ImageUSB)
1. Download **ImageUSB** by **PassMark Software**.
2. Plug in your USB flash drive and select it in ImageUSB.
3. Choose **Write image to USB drive** and select `build/anvilos_usb.bin` (or `build/anvilos_usb.img`).
4. Click **Write** and boot your PC from the USB drive (Legacy BIOS / UEFI CSM mode).

## Building from Source
```bash
make -j$(nproc) all zip  # Builds kernel, 91 OpenBSD .mod modules, USB image, Windows tools, and zips
make test-qemu           # Verifies selective module loading & .anv execution in QEMU
make test-qemu-nomod     # Verifies "No Compatible Devices On Your Computer! :" Continue & Shut Down
```
