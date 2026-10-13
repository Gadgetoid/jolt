ENTRY(WinMain)
SECTIONS
{
    . = 0x00801000;
    .text : { *(.text .text.*) }
    . = ALIGN(0x1000);
    .rdata : {
        *(.rodata .rodata.*)
        PROVIDE_HIDDEN(__init_array_start = ALIGN(4));
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = ALIGN(4));
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }
    . = ALIGN(0x1000);
    .data : { *(.iat) *(SORT_BY_NAME(.iat.*)) *(.data .data.* .sdata .sdata.*) *(.sbss .sbss.* .bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) *(.comment) *(.eh_frame) *(.note*) }
}