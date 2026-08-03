# pasinux CPU-Centric Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Implement paging, ring-3 user mode with INT 0x80 syscalls, and a higher-half kernel at 0xC0000000+ across three incremental stages.

**Architecture:** Three sequential stages, each independently buildable and testable in QEMU: identity paging (S1), user mode with TSS+GDT+syscall gate (S2), higher-half kernel with per-process page directories (S3). The hosted simulator is dropped —all code builds under `make image`.

**Tech Stack:** C11 `-ffreestanding -m32`, NASM (win32 obj format), ld (MinGW `-m i386pe`) — same as the existing freestanding build path.

## Global Constraints

- All new C files use `-ffreestanding`, no libc calls; use inline string helpers
- Object format: NASM `-f win32`, linker `-m i386pe`
- Kernel loads at physical 0x10000 (boot.asm contract) — never change this
- Every `_PRINT("...")` goes through `serial.c` only (no VGA first for newm. code paths)
- Existing drivers (VGA, serial, keyboard, timer) and shell must work unmodified at every stage

