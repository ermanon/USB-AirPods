ENTRY(run_tests)
SECTIONS {
    . = 0x10000;
    .text : { *(.text.run_tests) *(.text*) *(.rodata*) }
    . = 0x20000100;
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
}
