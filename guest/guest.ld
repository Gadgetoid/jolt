ENTRY(_start)
PHDRS
{
    headers PT_PHDR PHDRS;
    readonly PT_LOAD FILEHDR PHDRS FLAGS(5);
    note PT_NOTE FLAGS(4);
    writable PT_LOAD FLAGS(6);
    tls PT_TLS FLAGS(4);
}
SECTIONS
{
    . = 0x00010000 + SIZEOF_HEADERS;
    .note.jolt : { KEEP(*(.note.jolt)) } :readonly :note
    .text : { *(.text .text.*) } :readonly
    .rodata : { *(.rodata .rodata.*) } :readonly
    . = ALIGN(0x1000);
    .tdata : { *(.tdata .tdata.*) } :writable :tls
    .tbss : { *(.tbss .tbss.*) } :writable :tls
    .data : { *(.data .data.*) } :writable
    .bss : { *(.bss .bss.* COMMON) } :writable
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.eh_frame) *(.note*) }
}