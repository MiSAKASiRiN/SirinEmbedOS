ENTRY(Reset_Handler)
MEMORY {
    CODE (rx) : ORIGIN = 0x200c0000, LENGTH = 15K
    SNAP (rw) : ORIGIN = 0x200c3c00, LENGTH = 1K
    DATA (rw) : ORIGIN = 0x200c4000, LENGTH = 4K
}
_estack = ORIGIN(DATA) + LENGTH(DATA);
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > CODE
    .text : { *(.text .text.*) *(.rodata .rodata.*) } > CODE
    .resource_table : ALIGN(4) { KEEP(*(.resource_table)) } > CODE
    .ARM.exidx : { *(.ARM.exidx*) } > CODE
    .preinit_array : { __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .; } > CODE
    .init_array : { __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } > CODE
    .data : ALIGN(4) { _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > DATA
    _sidata = ADDR(.data);
    .bss (NOLOAD) : ALIGN(4) { _sbss = .; *(.bss .bss.* COMMON) KEEP(*(.remote_trace)) . = ALIGN(4); _ebss = .; } > DATA
    ASSERT(_ebss + 1024 <= _estack, "Insufficient M0+ stack")
    .srnos_ipc (NOLOAD) : { KEEP(*(.srnos_ipc)) } > SNAP
}
