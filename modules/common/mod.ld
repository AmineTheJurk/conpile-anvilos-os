ENTRY(mod_init)
SECTIONS {
    . = 0x00000000;
    .text : {
        *(.text*)
    }
    .rodata : ALIGN(4) {
        *(.rodata*)
    }
    .data : ALIGN(4) {
        *(.data*)
    }
    .bss : ALIGN(4) {
        *(COMMON)
        *(.bss*)
    }
    .modmeta : ALIGN(4) { *(.modmeta*) }
    .modids  : ALIGN(4) { *(.modids*) }
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
