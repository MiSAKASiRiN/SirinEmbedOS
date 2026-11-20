ENTRY(Reset_Handler)
MEMORY {
    VECTOR (rx) : ORIGIN = 0x80100000, LENGTH = 1536
    CODE (rx) : ORIGIN = 0x80100600, LENGTH = 8M - 1536
    DATA (rw) : ORIGIN = 0x80a00000, LENGTH = 1M
    IPC (rw) : ORIGIN = 0x81200000, LENGTH = 4K
    SNAP (rw) : ORIGIN = 0x81201000, LENGTH = 1K
}
_estack = ORIGIN(DATA) + LENGTH(DATA);
SECTIONS {
    .isr_vectors : { KEEP(*(.isr_vectors)) } > VECTOR
    .text : { *(.text .text.*) *(.rodata .rodata.*) } > CODE
    .ARM.exidx : { *(.ARM.exidx*) } > CODE
    .preinit_array : { __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .; } > CODE
    .init_array : { __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; } > CODE
    .data : ALIGN(8) { _sdata = .; *(.data .data.*) . = ALIGN(8); _edata = .; } > DATA
    _sidata = ADDR(.data);
    .tdata : ALIGN(8) { *(.tdata .tdata.*) } > DATA
    .tbss (NOLOAD) : ALIGN(8) { *(.tbss .tbss.*) } > DATA
    __tls_base = ADDR(.tdata);
    __tls_align = MAX(8, MAX(ALIGNOF(.tbss), ALIGNOF(.tdata)));
    __tls_size = ADDR(.tbss) + SIZEOF(.tbss) - __tls_base;
    __tdata_source = ADDR(.tdata);
    __tdata_size = SIZEOF(.tdata);
    __tbss_offset = ADDR(.tbss) - __tls_base;
    __tbss_size = SIZEOF(.tbss);
    __arm32_tls_tcb_offset = MAX(8, __tls_align);
    .bss (NOLOAD) : ALIGN(8) { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(8); _ebss = .; } > DATA
    .runtime (NOLOAD) : ALIGN(__tls_align) {
        __srnos_boot_tls = .;
        . += __tls_size;
        . = ALIGN(16);
        __srnos_libc_start = .;
        . += 8192;
        __srnos_libc_end = .;
    } > DATA
    ASSERT(__srnos_libc_end <= _estack - 8192, "M33 data and MSP overlap")
    .resource_table : ALIGN(16) { KEEP(*(.resource_table)) } > IPC
    .trace : ALIGN(32) { KEEP(*(.remote_trace)) } > IPC
    .srnos_ipc (NOLOAD) : { KEEP(*(.srnos_ipc)) } > SNAP
}
