/* user.ld — link script for pasinux ELF32 user programs. */
ENTRY(_start)
SECTIONS
{
    . = 0x08048000;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
