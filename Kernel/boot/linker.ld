/* Freestanding kernel layout.
 *
 * VMA = 0xC0010000 (higher half — kernel runs at 3 GB + 64 KB).
 * The boot sector loads the image at physical 0x10000.
 * Entry.asm sets up paging (PDE[768] → phys 0) then jumps to the higher-half VMA. */
ENTRY(_start)

SECTIONS
{
    . = 0xC0010000;

    .text : AT(0x10000) {
        *(.text)
        *(.text.*)
    }

    .rdata : {
        *(.rdata)
        *(.rdata.*)
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.reloc)
        *(.idata*)
        *(.CRT*)
        *(.tls*)
        *(.rdata$zzz)
        *(.pdata)
        *(.xdata)
        *(.eh_frame*)
    }
}