// Minimal ELF32 executable loader for ring-3 user programs.
#ifndef ELF_H
#define ELF_H

#include "fat12.h"

/* Load an ELF32 ET_EXEC image from the FAT12 root directory, map its
 * PT_LOAD segments plus a user stack into a fresh address space, then run
 * it in ring 3 until it invokes SYS_EXIT.
 *
 * Runs synchronously: this call does not return until the user program
 * exits. Returns 0 if the program ran (regardless of its exit code),
 * -1 if it could not be found / loaded / launched. */
int user_prog_exec(fat12_fs_t* fs, const char* name);

#endif // ELF_H
