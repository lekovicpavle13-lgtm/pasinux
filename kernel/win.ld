ENTRY(_start)
SECTIONS
{
    . = 0x8000;
    .data : {
        *(.data)
        bss_start = .;
        bss_end = .;
        kernel_main = _kernel_main;
    }
    .text : {
        *(.text)
        *(.text.*)
    }
}
