; AnvilOS Stage 2 Bootloader (8192 bytes, loaded at 0x7E00)
; - Enables A20
; - Queries E820 Memory Map
; - Loads kernel (LBA 17..272, 128KB) & AnvilFS initial snapshot (LBA 512..767, 128KB)
; - Negotiates VESA VBE 32-bit Linear Framebuffer (1024x768 -> 800x600 -> 640x480)
; - Switches to 32-bit Protected Mode, relocates kernel to 1MB & initrd to 4MB, jumps to kernel
BITS 16
ORG 0x7E00

BOOTINFO_ADDR   equ 0x5000
VBE_INFO_ADDR   equ 0x6000
VBE_MODE_ADDR   equ 0x6200

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    sti

    mov [s2_boot_drive], dl

    ; Clear boot_info_t (1024 bytes at 0x5000)
    mov di, BOOTINFO_ADDR
    mov cx, 512
    xor ax, ax
    rep stosw

    mov dword [BOOTINFO_ADDR + 0], 0x42564E41 ; "ANVB"
    movzx eax, byte [s2_boot_drive]
    mov [BOOTINFO_ADDR + 4], eax

    call enable_a20
    call detect_e820

    ; Load Kernel: 256 sectors (128KB) from LBA 17 into 0x10000..0x2FFFF
    mov eax, 17
    mov bx, 0x1000          ; segment 0x1000 -> phys 0x10000
    mov cx, 4               ; 4 * 64 sectors = 256 sectors
    call load_chunks_lba

    ; Load AnvilFS snapshot: 640 sectors (320KB) from LBA 512 into 0x30000..0x7FFFF
    mov eax, 512
    mov bx, 0x3000          ; segment 0x3000 -> phys 0x30000
    mov cx, 10
    call load_chunks_lba

    ; Setup VESA VBE graphics mode
    call setup_vbe

    ; Enter 32-bit Protected Mode
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm32_entry

; ------------------------------------------------------------
; A20 Gate
; ------------------------------------------------------------
enable_a20:
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    test al, 2
    jnz .done
    or al, 2
    and al, 0xFE
    out 0x92, al
.done:
    ret

; ------------------------------------------------------------
; E820 Memory Map -> bootinfo.e820 at BOOTINFO_ADDR + 40
; ------------------------------------------------------------
detect_e820:
    xor ebx, ebx
    mov di, BOOTINFO_ADDR + 40
    xor bp, bp
.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    inc bp
    add di, 24
    cmp bp, 32
    jae .done
    test ebx, ebx
    jnz .loop
.done:
    mov [BOOTINFO_ADDR + 26], bp
    ret

; ------------------------------------------------------------
; Load CX chunks of 64 sectors starting at EAX LBA into segment BX:0000
; ------------------------------------------------------------
load_chunks_lba:
    pusha
    mov [lba_cur], eax
    mov [seg_cur], bx
    mov [chunk_cnt], cx
.next_chunk:
    mov ax, [seg_cur]
    mov [s2_dap_seg], ax
    mov word [s2_dap_off], 0
    mov word [s2_dap_cnt], 64
    mov eax, [lba_cur]
    mov [s2_dap_lba], eax
    mov dword [s2_dap_lba + 4], 0

    mov si, s2_dap
    mov ah, 0x42
    mov dl, [s2_boot_drive]
    int 0x13
    jc .read_fail

    add dword [lba_cur], 64
    add word [seg_cur], 0x0800 ; +32KB in segment address
    dec word [chunk_cnt]
    jnz .next_chunk
    popa
    ret

.read_fail:
    mov si, msg_read_err
    call s2_print
.halt:
    cli
    hlt
    jmp .halt

s2_print:
    lodsb
    test al, al
    jz .r
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp s2_print
.r:
    ret

; ------------------------------------------------------------
; VESA VBE Mode Probe (1024x768x32 -> 800x600x32 -> 640x480x32)
; ------------------------------------------------------------
setup_vbe:
    mov dword [VBE_INFO_ADDR], 0x32454256 ; "VBE2"
    mov ax, 0x4F00
    mov di, VBE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    ; Try preferred resolutions in order
    mov word [target_w], 1024
    mov word [target_h], 768
    call find_vbe_mode
    jc .set_mode

    mov word [target_w], 800
    mov word [target_h], 600
    call find_vbe_mode
    jc .set_mode

    mov word [target_w], 640
    mov word [target_h], 480
    call find_vbe_mode
    jc .set_mode

.no_vbe:
    mov byte [BOOTINFO_ADDR + 25], 0
    ret

.set_mode:
    mov bx, [chosen_mode]
    or bx, 0x4000           ; Request Linear Framebuffer
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    mov eax, [VBE_MODE_ADDR + 40] ; PhysBasePtr
    mov [BOOTINFO_ADDR + 8], eax
    movzx eax, word [VBE_MODE_ADDR + 18] ; XResolution
    mov [BOOTINFO_ADDR + 12], eax
    movzx eax, word [VBE_MODE_ADDR + 20] ; YResolution
    mov [BOOTINFO_ADDR + 16], eax
    movzx eax, word [VBE_MODE_ADDR + 16] ; BytesPerScanLine
    mov [BOOTINFO_ADDR + 20], eax
    mov al, [VBE_MODE_ADDR + 25] ; BitsPerPixel
    mov [BOOTINFO_ADDR + 24], al
    mov byte [BOOTINFO_ADDR + 25], 1
    ret

; Returns CF=1 if found, mode in [chosen_mode]
find_vbe_mode:
    mov ax, [VBE_INFO_ADDR + 16] ; mode list segment
    mov fs, ax
    mov si, [VBE_INFO_ADDR + 14] ; mode list offset
.loop:
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .not_found
    add si, 2

    push si
    push cx
    mov ax, 0x4F01
    mov di, VBE_MODE_ADDR
    int 0x10
    pop cx
    pop si
    cmp ax, 0x004F
    jne .loop

    ; Check attributes: supported(0), graphics(4), LFB(7) -> 0x91
    mov ax, [VBE_MODE_ADDR + 0]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .loop

    cmp byte [VBE_MODE_ADDR + 25], 32 ; 32 bpp
    jne .loop
    cmp byte [VBE_MODE_ADDR + 27], 6  ; Direct Color
    jne .loop

    mov ax, [VBE_MODE_ADDR + 18]
    cmp ax, [target_w]
    jne .loop
    mov ax, [VBE_MODE_ADDR + 20]
    cmp ax, [target_h]
    jne .loop

    mov [chosen_mode], cx
    stc
    ret
.not_found:
    clc
    ret

; ------------------------------------------------------------
; 32-bit Protected Mode Entry
; ------------------------------------------------------------
BITS 32
pm32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000

    ; Copy kernel (128KB) from 0x00010000 -> 0x00100000
    mov esi, 0x00010000
    mov edi, 0x00100000
    mov ecx, 32768          ; 131072 / 4
    cld
    rep movsd

    ; Copy AnvilFS initrd (320KB) from 0x00030000 -> 0x00400000
    mov esi, 0x00030000
    mov edi, 0x00400000
    mov ecx, 81920
    rep movsd

    mov dword [BOOTINFO_ADDR + 28], 0x00400000 ; initrd_addr
    mov dword [BOOTINFO_ADDR + 32], 327680     ; initrd_size
    mov dword [BOOTINFO_ADDR + 36], 131072     ; kernel_size

    push dword BOOTINFO_ADDR
    call 0x00100000

.halt32:
    cli
    hlt
    jmp .halt32

; ------------------------------------------------------------
; Data & GDT
; ------------------------------------------------------------
s2_boot_drive: db 0
lba_cur:       dd 0
seg_cur:       dw 0
chunk_cnt:     dw 0
target_w:      dw 0
target_h:      dw 0
chosen_mode:   dw 0
msg_read_err:  db "Stage2 disk err!", 13, 10, 0

align 4
s2_dap:
    db 0x10
    db 0x00
s2_dap_cnt: dw 64
s2_dap_off: dw 0
s2_dap_seg: dw 0
s2_dap_lba: dq 0

align 8
gdt_start:
    dq 0x0000000000000000   ; Null
    dq 0x00CF9A000000FFFF   ; Code 0x08: base 0, limit 4GB, RX
    dq 0x00CF92000000FFFF   ; Data 0x10: base 0, limit 4GB, RW
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 8192 - ($ - $$) db 0
