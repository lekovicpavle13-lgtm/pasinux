#ifndef TUI_SHELL_H
#define TUI_SHELL_H

#include "fat12.h"

void tui_shell_init(fat12_fs_t* fs);
void vga_shell_run_tui(const char* line);
void tui_shell_open_about(void);
void tui_shell_open_clock(void);

#endif