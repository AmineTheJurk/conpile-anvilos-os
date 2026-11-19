CC      := gcc
WIN_CC  := x86_64-w64-mingw32-gcc
LD      := ld
OBJCOPY := objcopy
NASM    := nasm

KCFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc \
           -std=gnu11 -fno-builtin -Wall -Wextra -O2 -Iinclude -D__ANVIL_KERNEL__
MCFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostdinc \
           -std=gnu11 -fno-builtin -fno-asynchronous-unwind-tables -Wall -Wextra -Os \
           -Iinclude -D__ANVIL_KERNEL__
HFLAGS  := -std=gnu11 -O2 -Wall -Wextra

BUILD   := build

K_ASM   := kernel/entry.asm
K_SRCS  := kernel/kernel.c kernel/gdt.c kernel/idt.c kernel/mem.c kernel/string.c \
           kernel/task.c kernel/anv_loader.c kernel/kmod.c kernel/wifi.c kernel/netstack.c \
           drivers/serial.c drivers/timer.c drivers/keyboard.c drivers/mouse.c \
           drivers/ata.c drivers/pci.c drivers/fb.c drivers/acpi.c drivers/rtc.c \
           fs/anvilfs.c fs/vfs.c \
           gui/terminal.c gui/apps.c gui/desktop.c

K_OBJS  := $(BUILD)/entry.o $(patsubst %.c,$(BUILD)/%.o,$(K_SRCS))

MOD_SRCS := $(sort $(wildcard modules/*/*.c))
MOD_BINS := $(patsubst %.c,$(BUILD)/modules/%.mod,$(notdir $(MOD_SRCS)))

.PHONY: all clean check zip windows test-qemu test-qemu-nomod

all: $(BUILD)/anvilos_usb.bin $(BUILD)/anvilos_usb.img $(BUILD)/agcc $(BUILD)/agcc.exe $(BUILD)/mkanvilfs.exe $(BUILD)/mkmod.exe

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

$(BUILD)/entry.o: $(K_ASM)
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

$(BUILD)/kernel.elf: $(K_OBJS) kernel.ld
	$(LD) -m elf_i386 -T kernel.ld -o $@ $(K_OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@sz=$$(stat -c%s $@); echo "[BUILD] kernel.bin size: $$sz bytes (max 131072)"; \
	if [ $$sz -gt 131072 ]; then echo "ERROR: kernel.bin exceeds 128KB"; exit 1; fi

$(BUILD)/mbr.bin: boot/mbr.asm
	@mkdir -p $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm
	@mkdir -p $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/agcc: compiler/agcc.c include/anv.h
	@mkdir -p $(BUILD)
	$(CC) $(HFLAGS) compiler/agcc.c -o $@

$(BUILD)/agcc.exe: compiler/agcc.c include/anv.h
	@mkdir -p $(BUILD)
	$(WIN_CC) $(HFLAGS) compiler/agcc.c -o $@

$(BUILD)/mkanvilfs: tools/mkanvilfs.c include/anvilfs.h
	$(CC) $(HFLAGS) tools/mkanvilfs.c -o $@

$(BUILD)/mkanvilfs.exe: tools/mkanvilfs.c include/anvilfs.h
	$(WIN_CC) $(HFLAGS) tools/mkanvilfs.c -o $@

$(BUILD)/mkmod: tools/mkmod.c include/kmod.h
	$(CC) $(HFLAGS) tools/mkmod.c -o $@

$(BUILD)/mkmod.exe: tools/mkmod.c include/kmod.h
	$(WIN_CC) $(HFLAGS) tools/mkmod.c -o $@

# Compile each AnvilOS loadable module as an AKM1 binary.
define MOD_RULE
$(BUILD)/modules/$(notdir $(1:.c=.mod)): $(1) include/kmod.h modules/common/mod.ld $(BUILD)/mkmod
	@mkdir -p $(BUILD)/mod_obj $(BUILD)/modules
	$(CC) $(MCFLAGS) -c $$< -o $(BUILD)/mod_obj/$(notdir $(1:.c=.o))
	$(LD) -m elf_i386 --no-warn-rwx-segments -q -T modules/common/mod.ld -e mod_init $(BUILD)/mod_obj/$(notdir $(1:.c=.o)) -o $(BUILD)/mod_obj/$(notdir $(1:.c=.elf))
	./$(BUILD)/mkmod build $(BUILD)/mod_obj/$(notdir $(1:.c=.elf)) $$@
endef

$(foreach s,$(MOD_SRCS),$(eval $(call MOD_RULE,$(s))))

$(BUILD)/modules/modules.idx: $(MOD_BINS) $(BUILD)/mkmod
	./$(BUILD)/mkmod index $@ $(MOD_BINS)

$(BUILD)/hello.anv: compiler/examples/hello.nc $(BUILD)/agcc
	./$(BUILD)/agcc $< -o $@

$(BUILD)/sysinfo.anv: compiler/examples/sysinfo.nc $(BUILD)/agcc
	./$(BUILD)/agcc $< -o $@

$(BUILD)/calc.anv: compiler/examples/calc.nc $(BUILD)/agcc
	./$(BUILD)/agcc $< -o $@

$(BUILD)/anvilos_fs.manifest: $(MOD_BINS) $(BUILD)/modules/modules.idx $(BUILD)/hello.anv $(BUILD)/sysinfo.anv $(BUILD)/calc.anv
	@mkdir -p $(BUILD)
	@rm -f $@
	@echo "$(BUILD)/hello.anv:/bin/hello.anv" >> $@
	@echo "$(BUILD)/sysinfo.anv:/bin/sysinfo.anv" >> $@
	@echo "$(BUILD)/calc.anv:/bin/calc.anv" >> $@
	@echo "$(BUILD)/modules/modules.idx:/lib/modules/modules.idx" >> $@
	@for m in $(MOD_BINS); do echo "$$m:/lib/modules/$$(basename $$m)" >> $@; done

$(BUILD)/anvilos_usb.bin: $(BUILD)/mbr.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
                          $(BUILD)/mkanvilfs $(BUILD)/anvilos_fs.manifest
	./$(BUILD)/mkanvilfs $(BUILD)/mbr.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $@ @$(BUILD)/anvilos_fs.manifest

$(BUILD)/anvilos_usb.img: $(BUILD)/anvilos_usb.bin
	cp $< $@

windows: $(BUILD)/agcc.exe $(BUILD)/mkanvilfs.exe $(BUILD)/mkmod.exe

check:
	@for f in $(K_SRCS); do \
		$(CC) $(KCFLAGS) -fsyntax-only $$f || exit 1; \
	done
	$(CC) $(HFLAGS) -fsyntax-only compiler/agcc.c
	$(CC) $(HFLAGS) -fsyntax-only tools/mkanvilfs.c
	$(CC) $(HFLAGS) -fsyntax-only tools/mkmod.c
	@echo "[CHECK] All kernel & host tool C sources passed strict syntax & warning checks."

zip: all
	@rm -f anvilos-complete.zip $(BUILD)/anvilos-source.zip nyxos-complete.zip
	zip -rq $(BUILD)/anvilos-source.zip .github boot compiler drivers fs gui include kernel modules tools scripts public kernel.ld Makefile README.md
	zip -rq anvilos-complete.zip .github boot compiler drivers fs gui include kernel modules tools scripts public kernel.ld Makefile README.md \
		$(BUILD)/anvilos_usb.bin $(BUILD)/anvilos_usb.img $(BUILD)/agcc.exe $(BUILD)/mkanvilfs.exe $(BUILD)/mkmod.exe \
		$(BUILD)/modules $(BUILD)/hello.anv $(BUILD)/sysinfo.anv $(BUILD)/calc.anv
	@echo "[ZIP] Created anvilos-complete.zip and $(BUILD)/anvilos-source.zip"

test-qemu: $(BUILD)/anvilos_usb.bin
	@echo "Running AnvilOS Wi-Fi & selective OpenBSD module boot test in QEMU..."
	(sleep 2; printf "uname\nlsmod\ndate\nwifi scan\nwifi connect AnvilNet-5G pass123\nwifi\n/bin/hello.anv\n"; sleep 2) | \
		timeout 12 qemu-system-i386 -drive file=$(BUILD)/anvilos_usb.bin,format=raw,index=0,media=disk -m 64M -nographic -serial mon:stdio || true

test-qemu-nomod: $(BUILD)/anvilos_usb.bin
	@echo "Testing 'No Compatible Devices On Your Computer! :' Continue & Shut Down paths..."
	(sleep 2; printf "\r"; sleep 1; printf "uname\nlsmod\n"; sleep 1) | \
		timeout 10 qemu-system-i386 -drive file=$(BUILD)/anvilos_usb.bin,format=raw,index=0,media=disk -device es1370 -m 64M -nographic -serial mon:stdio || true
	@echo "--- Testing arrow-key navigation to Shut Down ---"
	(sleep 2; printf "\033[C\r"; sleep 1) | \
		timeout 10 qemu-system-i386 -drive file=$(BUILD)/anvilos_usb.bin,format=raw,index=0,media=disk -device es1370 -m 64M -nographic -serial mon:stdio || true

clean:
	rm -rf $(BUILD) anvilos-complete.zip nyxos-complete.zip
