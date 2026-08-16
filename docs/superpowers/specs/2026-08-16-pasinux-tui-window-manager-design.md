# pasinux — Polished text-mode UI: a reusable TUI toolkit + overlapping window manager

Date: 2026-08-16
Status: Approved (design sections 1–5)

## 1. Goal

Give pasinux a polished text-mode user interface: a small reusable text-UI
toolkit, an **overlapping window manager** that composes windows in z-order onto
the 80×25 VGA screen, and a rebuilt interactive shell that runs as a window in it.
The toolkit is built to be reused by future apps (notably the deferred notepad).

This makes the kernel's interactive front end feel like a real desktop, controlled
by the PS/2 mouse (new driver) and by keyboard equivalents, without leaving text mode.

## 2. Decisions locked during brainstorming

- **Scope:** polished text-mode UI (not graphics-mode bitmap GUI, not a fullscreen TUI only).
- **Architecture:** build a reusable text-UI layer first, then rebuild the shell on it.
- **Window model:** truly overlapping, draggable text windows with a z-ordered compositor.
- **Input:** PS/2 mouse **and** keyboard chords (Alt+Tab / Alt+arrows / Ctrl+D / F2 / F3 / Esc).
- **Compositing:** **Approach A** — per-window off-screen surfaces, bottom-to-top full redraw per frame, rect-clipped. (80×25 ≈ 2 KB, so full redraws are cheap; `cli()/sti()` around the composite.)
- **Frame style:** single-width border drawn with box-drawing glyphs; the **title bar is the top border row**. Focused window's title bar is brighter.
- **Default desktop:** on boot the **shell window fills the screen**; **About and Clock are already open** at boot.
- **Window move:** continuous follow-drag while the mouse is held; windows **clamp fully on-screen** (never partially off-edge).
- **Cursor:** a **solid inverted block (█)** overlaid last by the compositor.
- **Scrolling:** implicit (arrow keys / readline) for now; a visible scrollbar glyph is deferred.
- **Clock content:** **real wall-clock** from a new CMOS RTC driver (not an uptime counter).

## 3. Module inventory

```
tui/tui_core.c/h    cell/color types, tui_surface, compositor, cursor overlay
tui/tui_wm.c/h      window_t, z-order, focus, lifecycle ops, event loop (tui_run)
tui/widgets.c/h     tui_readline widget (built on surfaces)
drivers/ps2mouse.c/h  PS/2 mouse driver (IRQ12)
drivers/rtc.c/h     CMOS RTC wall-clock reader
drivers/vga.c/h     ADD per-cell-attribute functions (existing text API unchanged)
kernel/kmain.c      replace vga_shell_interactive() with WM init + tui_run(); channel shell output to window
Makefile            add new .o files + -Itui
```

## 4. Section detail

### 4.1 Render layer (tui_core)

Today `vga.c` writes every cell with hardcoded `0x0F`. The TUI needs per-cell color:

- Add to `drivers/vga.c`: `vga_putc_attr(char c, uint8_t attr)` and
  `vga_cell(size_t row, size_t col, char c, uint8_t attr)` writing straight to `0xC00B8000`.
  The existing pure-text `vga_puts`/`vga_write` stay untouched for kernel logs/boot serial.
- Attribute byte uses standard VGA layout: `blink | backup(3) | foreground(4)`.
- A color palette enum (`TUI_BLACK..TUI_WHITE`) and a default theme mapping:
  window border cyan, focused title bar bright-white-on-blue, shell text light-gray,
  errors bright-red, prompts bright-green.
- **Surface:** an off-screen buffer of cells (`uint16_t` per cell, exactly like real VGA).
  `tui_surface_clear`, `tui_surface_write(surface,row,col,text,attr)`,
  `tui_surface_blit_to(surface, dst_row, dst_col, rect)`.
- **Compositor (Approach A):** keeps an ordered window list; `tui_composite()` clears VGA
  to background, then blits windows bottom-to-top, each clipped to
  `max(0,wx)..min(wx+w,79) × max(0,wy)..min(wy+h,24)`. Then overlays the cursor block last.

### 4.2 Window manager (tui_wm)

- **`tui_win_t`:** `x,y,w,h`, a content-surface (`w*h` cells), `title[]`, visibility,
  z-index, `app` payload. Each window self-draws via an `on_draw(surface, win)` callback
  with content-relative coordinates; windows never touch VGA directly.
- **Lifecycle/z-ops:** `tui_win_create(title,w,h)` (kmalloc surface, insert on top),
  `tui_win_raise`, `tui_win_move(dx,dy)` (clamp to 80×25, redraw),
  `tui_win_close` (free surface, focus falls to next-topmost), `tui_win_cycle_focus` (Alt+Tab).
- **Focus:** one focused window; receives keyboard; its title bar is brighter.
- **Event loop `tui_run()`:** drain keyboard queue → build key event `{ascii, scancode,
  alt, ctrl, shift}`; Alt+Tab cycles focus, else route to focused window's `on_key`.
  Drain mouse queue → click over a window focuses+raises; click+drag on its title bar
  continuously moves it; else post to hovered window. Redraw on change, then `hlt` until
  next IRQ (no busy spin).
- **Keyboard extension:** add to `keyboard.c` an event queue `{ascii, scancode, alt, ctrl,
  shift}` from the existing scancode handler, with `keyboard_set_mode()` so the plain
  readline path still works.

### 4.3 Mouse + cursor (ps2mouse)

- PS/2 auxiliary device on IRQ12 (port 0x60 data; 0x64 command byte to enable IRQ12 +
  write 0xD4). Three-button, no wheel: packet = 3 bytes → signed ΔX, ΔY + 3 button bits.
- Init: enable auxiliary device, stream mode, sample rate. Accumulate deltas into a
  shared `mouse_state_t` (instantaneous position for dragging). `mouse_enable/disable`.
- Delta→cell scaling (±N deltas ≈ one cell nudge) so movement is usable across 80×25.
- Click hit-test maps cell → window topmost-first.
- **Keyboard equivalents:** Alt+Tab cycle, Alt+↑/↓/←/→ move focus one cell, Ctrl+D
  commit drag; F2=About, F3=Clock, Esc closes focus window.

### 4.4 Widgets + shell window

- **`tui_readline` widget:** prompt + attr, cursor left/right, insert, backspace,
  Home/End, Up/Down history recall (bounded ring, kmalloc, last 32 lines). Returns buffer.
- **Shell window:** content = scrollable text region (ring buffer of lines, each tagged
  with an attr) + a bottom `tui_readline` input row. Runs the **existing command set
  unchanged**, with output redirected to the window sink. Styling: commands white, errors
  bright-red, FS results light-gray, prompt bright-green.
- **Sample windows:** Shell (fills screen, anchored bottom of z-order);
  **About** (small centered static banner, closes on Esc);
  **Clock** (top-right, redraws each second from `rtc_read_time`).
  About and Clock are **open at boot**, stacked a couple rows apart to show z-order.

### 4.5 RTC + integration + build

- **RTC driver:** read via 0x70/0x71, poll status-register-A update flag (0x80) for a
  stable read, BCD→binary. `rtc_read_time(int*h,int*m,int*s)`.
- **Integration (`kmain.c`):** after `freestanding_subsystems_up()` + FS selftests, init
  WM + mouse, spawn Shell/About/Clock, call `tui_run()`. Reuse the existing command
  dispatcher; channel its output into the shell window stream.
- **Build:** add `drivers/ps2mouse.o`, `drivers/rtc.o`, `tui/tui_core.o`,
  `tui/tui_wm.o`, `tui/widgets.o` to `FREE_OBJS`; add `-Itui` to `FREE_CFLAGS`.

## 5. Testing

- **Boot-time selftest (headless via serial, regression-safe):** after WM init, script the
  compositor directly — create a synthetic window, composite, assert the VGA framebuffer at
  known coordinates holds the expected border glyph; move the window and assert cells
  relocated and the old region repainted. Log `[TUI] compositor selftest: OK`.
- **Interactive (VirtualBox VBoxVGA or QEMU):** drag Clock/About by title bar (continuous,
  clamped); Alt+Tab cycle focus; Alt+arrows nudge; Esc closes About, F2 reopens; Clock
  ticks every second; run `ls`/`touch`/`write` in the window to confirm FS works inside it.

## 6. Out of scope

- Graphics (bitmap) mode, fonts beyond the VGA 8×16 set, real image windows.
- Wheel scrolling, per-window resize handles, drag-and-drop between windows.
- Persistent history across reboots (no file backing for readline history).
- The notepad app itself (it will consume this toolkit as a follow-on).
- Mouse-driven cross-window drag-and-drop or copy/paste.