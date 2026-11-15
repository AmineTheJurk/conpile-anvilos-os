; AnvilOS Stage 1 MBR Bootloader (512 bytes)
; Compatible with USB flash drives (PassMark ImageUSB), BIOS HDD, and QEMU.
BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    jmp 0x0000:.init_cs

.init_cs:
    sti
    mov [boot_drive], dl

    mov si, msg_boot
    call print_str

    ; Check INT 13h LBA extensions
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .chs_fallback
    cmp bx, 0xAA55
    jne .chs_fallback

    ; Load Stage 2 (16 sectors from LBA 1 -> 0x0000:0x7E00)
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .chs_fallback
    jmp .jump_stage2

.chs_fallback:
    mov ax, 0x0210          ; AH=02 read, AL=16 sectors
    mov cx, 0x0002          ; Cyl 0, Sector 2 (LBA 1)
    mov dh, 0x00            ; Head 0
    mov dl, [boot_drive]
    mov bx, 0x7E00
    int 0x13
    jc disk_error

.jump_stage2:
    mov dl, [boot_drive]
    jmp 0x0000:0x7E00

disk_error:
    mov si, msg_err
    call print_str
.hang:
    cli
    hlt
    jmp .hang

print_str:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_str
.done:
    ret

boot_drive: db 0
msg_boot:   db "AnvilOS MBR...", 13, 10, 0
msg_err:    db "Disk err!", 13, 10, 0

align 4
dap:
    db 0x10                 ; size of DAP (16 bytes)
    db 0x00                 ; reserved
    dw 16                   ; sector count (8 KB Stage 2)
    dw 0x7E00               ; offset
    dw 0x0000               ; segment
    dq 1                    ; starting LBA = 1

; Pad up to MBR Partition Table at offset 446 (0x1BE)
times 446 - ($ - $$) db 0

; Partition 1: Active Bootable AnvilFS Partition (LBA 512, 15872 sectors = 7.75 MiB)
part1:
    db 0x80                 ; Bootable flag
    db 0x00, 0x02, 0x00     ; CHS start
    db 0x83                 ; Type: Unix/AnvilFS
    db 0xFE, 0xFF, 0xFF     ; CHS end
    dd 512                  ; Start LBA
    dd 15872                ; Sector count

; Partitions 2..4 empty
times 48 db 0

dw 0xAA55
