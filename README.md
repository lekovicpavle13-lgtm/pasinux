# Pasinux

A hobby x86 bare-metal OS kernel written in C and NASM assembly, exploring low-level systems programming and kernel architecture.

67
## Overview

Pasinux is an educational x86 kernel project focused on implementing core OS functionality from scratch. The kernel demonstrates fundamental concepts in memory management, process scheduling, and hardware interaction through hands-on implementation.

**Current Status:** Under active development with a stabilizing foundation.

## Architecture

### Build System
- **Makefile-based compilation** with cross-compiler support
- **NASM assembly** for bootloader and CPU-level operations
- **GCC C compiler** for core kernel implementation
- Target: 32-bit x86 architecture (with provisions for x86_64 expansion)

### Core Modules

#### Boot & Hardware
- **Bootloader** (boot.s): Multiboot2-compliant entry point with CPU identification
- **CPU Operations** (cpu.c/cpu.h): CPU initialization, GDT setup, privileged mode transitions
- **Interrupt Handling** (interrupts.c/interrupts.h): IDT configuration, exception handlers, IRQ dispatch

#### Memory Management
- **Physical Memory Manager** (mm.c/mm.h): Page allocation, free list management, memory tracking
- **Virtual Memory**: Paging infrastructure with higher-half kernel mapping support
- **Memory Layouts**: Defines kernel memory regions (text, data, BSS)

#### Core Runtime
- **Kernel Entry Point** (kernel.c): Primary kernel initialization routine
- **Logging & Debug** (printk): Kernel logging to serial port / video memory
- **Hardware Utilities** (io.c): Port I/O, hardware communication primitives

#### Build Infrastructure
- **Linker Script** (kernel.ld): Memory layout and section organization
- **Multiboot2 Support**: Header and compliance for bootloader handoff

## Getting Started

### Prerequisites

- **GCC cross-compiler** for i686-elf target
- **NASM** (Netwide Assembler)
- **GNU Make**
- **QEMU** or similar x86 emulator (for testing)

### Building

```bash
make clean
make
```

The build process:
1. Compiles boot code (NASM → object files)
2. Compiles kernel C code with proper CPU flags
3. Links against the kernel linker script
4. Produces `kernel.bin` (raw binary) and optionally `kernel.elf` (with symbols)

### Testing

```bash
qemu-system-i386 -kernel kernel.bin
```

Or with additional debugging:
```bash
qemu-system-i386 -kernel kernel.bin -serial stdio
```

## Project History & Key Milestones

### Foundation Work (Earlier)
- Initial bare-metal x86 bootloader and kernel skeleton
- Basic x86_64 linker script for higher-half Multiboot2 layout
- Early memory management infrastructure

### Recent Reconstruction
- **Audit & Repair** (2024): Comprehensive code audit identified intermediate state from incomplete AI-assisted rewrite
  - Identified corrupted `mm.c` with missing allocation logic
  - Fixed conflicting kernel entry points
  - Corrected Makefile compilation flags and linking issues
- **Rewrite Cycle**: Clean implementation of core modules
  - Rebuilt `mm.c` with stable physical memory allocator
  - Refactored `kernel.c` with proper CPU initialization sequence
  - Resolved build system issues
- **Stability**: Passed compilation and sandbox testing post-rebuild

## Known Issues & Future Work

### Current Limitations
- **Limited Hardware Support**: VGA text mode only (no BIOS extensions)
- **No Scheduling**: Kernel runs in single-threaded mode; no process/task switching
- **No File System**: No disk I/O or persistent storage interaction
- **No User Mode**: Kernel runs entirely in ring-0; no user-mode execution or privilege separation

### Planned Enhancements
- Process/task management with basic scheduling
- User-mode process execution and syscall interface
- Virtual memory demand paging
- Simple file system (FAT or custom)
- Timer interrupts and time-keeping
- Multiprocessor support (SMP)

## Code Quality Notes

This project prioritizes clarity and learning value over production robustness. Key practices:

- **Explicit implementations**: Core functionality built from first principles
- **Modular design**: Separate concerns (memory, CPU, interrupts) with clean interfaces
- **Documentation**: Comments explain architectural decisions and non-obvious code paths
- **Testing**: Compilation and sandbox execution verify correctness

## Project Structure

```
pasinux/
├── boot.s                # Bootloader (Multiboot2, NASM)
├── kernel.c              # Kernel entry and main logic
├── kernel.ld             # Linker script (memory layout)
├── kernel.h              # Kernel-wide definitions
│
├── cpu.c / cpu.h         # CPU setup (GDT, privileged ops)
├── interrupts.c / interrupts.h  # IDT, exception handlers
├── io.c / io.h           # Port I/O, hardware primitives
├── printk.c / printk.h   # Logging to serial/VGA
│
├── mm.c / mm.h           # Physical memory manager
├── memory.h              # Memory region definitions
│
├── Makefile              # Build configuration
└── README.md             # This file
```

## Technical Highlights

### x86 Boot Process
The kernel follows the Multiboot2 specification, allowing compatibility with GRUB and other bootloaders. The boot.s entry point:
- Validates Multiboot magic number
- Sets up a minimal stack
- Calls the kernel's C entry point

### Memory Management
Physical memory allocation uses a simple free-list allocator:
- Tracks allocated and free pages
- Supports allocation and deallocation
- Foundation for virtual memory and paging

### Interrupt Handling
Provides a framework for CPU exceptions and hardware interrupts:
- IDT population with exception vectors
- Handlers for common faults (page fault, general protection fault, etc.)
- IRQ dispatch for future hardware device interaction

### CPU Initialization
Sets up critical CPU structures before main kernel execution:
- Global Descriptor Table (GDT) for privilege levels and memory segmentation
- Task State Segment (TSS) for privilege transitions
- CPU feature detection and configuration

## Learning Resources

This project is ideal for understanding:
- **Assembly Language**: x86 boot sequence and low-level CPU operations
- **Memory Management**: Page allocation, virtual memory fundamentals
- **OS Architecture**: Kernel structure, privilege separation, interrupt handling
- **Build Systems**: Makefiles, linker scripts, cross-compilation
- **Embedded Systems**: Hardware initialization, hardware-software interaction

## Contributing & Extending

The codebase is structured to be modular and extensible. To add new functionality:

1. **New Modules**: Create `module.c` / `module.h` files following the existing pattern
2. **Build Integration**: Add compilation rules to Makefile
3. **API Definitions**: Declare public interfaces in header files
4. **Documentation**: Comment architectural decisions in code

## Licensing

[Specify your license here — e.g., MIT, GPL, or Unlicensed]

## Acknowledgments

- **x86 Architecture References**: Intel and AMD documentation
- **Multiboot2 Specification**: GNU GRUB community
- **Inspiration**: OSDev.org community and educational OS projects

## Contact & Feedback

For questions, issues, or suggestions, please open an issue on the GitHub repository.

---

**Remember**: This is a hobby project for learning. Real production kernels (Linux, Windows, macOS) solve many additional problems around performance, security, and hardware compatibility that are simplified or omitted here.
