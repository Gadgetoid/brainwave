ENTRY(WinMain)
SECTIONS
{
    . = 0x11000;
    .text : { *(.text .text.*) }
    . = ALIGN(0x1000);
    .rdata : {
        *(.rodata .rodata.*)
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }
    . = ALIGN(0x1000);
    .data : { *(.iat) *(SORT_BY_NAME(.iat.*)) *(.data .data.* .sdata .sdata.*) *(.sbss .sbss.* .bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) *(.comment) *(.eh_frame) *(.note*) }
}