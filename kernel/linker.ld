
ENTRY(_start)

/* Kernels are conventionally loaded at 1MiB - low enough to avoid legacy
 * BIOS/VGA memory holes below it, high enough that real-mode code
 * (0x0-0x100000) doesn't collide with it. */
KERNEL_LOAD_ADDR = 1M;

SECTIONS
{
  . = KERNEL_LOAD_ADDR;

  _kernel_start = .;

  /* Entry stub + kernel code. .text.entry (if you use that section name
   * in entry.asm/boot.asm) is placed first so it's guaranteed to be at
   * KERNEL_LOAD_ADDR, matching where boot.asm jumps to. */
  .text ALIGN(4K) :
  {
    *(.text.entry)
    *(.text)
    *(.text*)
  }

  .rodata ALIGN(4K) :
  {
    *(.rodata)
    *(.rodata*)
  }

  .data ALIGN(4K) :
  {
    _data_start = .;
    *(.data)
    *(.data*)
    _data_end = .;
  }

  /* Zero-initialized data. Not present in the on-disk image; your entry
   * stub must zero [_bss_start, _bss_end) before calling kernel_main(),
   * since a plain BIOS disk-sector loader won't do this for you. */
  .bss ALIGN(4K) :
  {
    _bss_start = .;
    *(COMMON)
    *(.bss)
    *(.bss*)
    _bss_end = .;
  }

  _kernel_end = .;

  /* Discard sections that don't make sense in a freestanding binary. */
  /DISCARD/ :
  {
    *(.comment)
    *(.eh_frame)
    *(.note.GNU-stack)
  }
}

/* Handy size symbol for boot.asm's disk-read loop, e.g.:
 *   number of sectors to read = (_kernel_size + 511) / 512
 */
_kernel_size = _kernel_end - _kernel_start;
