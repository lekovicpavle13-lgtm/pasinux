#!/usr/bin/env python3
"""End-to-end QEMU test: boot pasinux, launch the ring-3 NOTEPAD.BIN ELF
from the TUI shell, edit/save a file through syscalls, exit, and verify
the bytes actually landed in pasinux.img (durable FAT12 write).

Drives the guest entirely through the QEMU monitor (sendkey/mouse), so it
works headless. Artifacts: test_serial.txt, test_cat.ppm.

Usage: python tools/e2e_notepad.py [--skip-build]
"""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
from pathlib import Path

import struct

KERNEL_DIR = Path(__file__).resolve().parent.parent
IMAGE = KERNEL_DIR / "pasinux.img"
SERIAL_LOG = KERNEL_DIR / "test_serial.txt"
SCREENSHOT = KERNEL_DIR / "test_cat.ppm"
MONITOR_PORT = 4445
NOTE_TEXT = "hello world"


class Monitor:
    def __init__(self, port: int) -> None:
        for _ in range(50):
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                time.sleep(0.3)
        else:
            raise SystemExit("could not connect to QEMU monitor")
        self.sock.settimeout(0.05)
        self._drain()

    def _drain(self) -> bytes:
        out = b""
        try:
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                out += chunk
        except (socket.timeout, ConnectionResetError, OSError):
            pass
        return out

    def cmd(self, command: str) -> None:
        self.sock.sendall((command + "\n").encode())
        time.sleep(0.02)
        self._drain()


def key(mon: Monitor, name: str) -> None:
    mon.cmd(f"sendkey {name}")
    time.sleep(0.04)


def type_string(mon: Monitor, s: str) -> None:
    named = {" ": "spc", ".": "dot"}
    for ch in s:
        if ch == ":":
            key(mon, "shift-semicolon")
        else:
            key(mon, named.get(ch, ch))


def press_enter(mon: Monitor) -> None:
    key(mon, "ret")


def wait_serial(marker: str, timeout: float = 90.0) -> str:
    deadline = time.time() + timeout
    data = ""
    while time.time() < deadline:
        if SERIAL_LOG.exists():
            data = SERIAL_LOG.read_text(errors="replace")
            if marker in data:
                return data
        time.sleep(0.4)
    raise SystemExit(
        f"TIMED OUT waiting for {marker!r}.\n--- last serial output ---\n"
        + "\n".join(data.splitlines()[-30:])
    )


def verify_image_content() -> None:
    img = IMAGE.read_bytes()
    root = img[19 * 512 : 33 * 512]
    fat = img[512 : 512 + 9 * 512]

    def fat_get(c: int) -> int:
        off = c + (c >> 1)
        w = fat[off] | (fat[off + 1] << 8)
        return (w >> 4) if (c & 1) else (w & 0xFFF)

    found = None
    for slot in range(224):
        e = root[slot * 32 : (slot + 1) * 32]
        if e[0] == 0x00:
            break
        if e[0] == 0xE5:
            continue
        name = e[0:11].decode("ascii", "replace")
        if e[0:8] == b"TEST    " and e[8:11] == b"TXT":
            cluster = struct.unpack("<H", e[26:28])[0]
            size = struct.unpack("<I", e[28:32])[0]
            chunks = []
            c = cluster
            remaining = size
            while remaining > 0 and c < 0xFF8:
                lba = 33 + (c - 2)
                chunk = img[lba * 512 : lba * 512 + min(remaining, 512)]
                chunks.append(chunk)
                remaining -= len(chunk)
                c = fat_get(c)
            found = b"".join(chunks)
            break

    if found is None:
        raise SystemExit("FAIL: TEST.TXT not found in pasinux.img")

    expect = (NOTE_TEXT + "\n").encode()
    if found != expect:
        raise SystemExit(
            f"FAIL: TEST.TXT content mismatch\n  expected: {expect!r}\n"
            f"  actual:   {found!r}"
        )
    print("PASS: TEST.TXT on disk == %r" % found)


def focus_shell(mon: Monitor) -> None:
    """Move the pointer into the pinned-bottom Shell window and click.
    Empirical calibration: monitor Y axis is inverted relative to the PS/2
    driver; -48 raw from screen center lands on row 24, col 40, which only
    the Shell window covers."""
    mon.cmd("mouse_move 0 -48")
    time.sleep(0.5)
    mon.cmd("mouse_button 1")
    time.sleep(0.15)
    mon.cmd("mouse_button 0")
    time.sleep(0.5)
    data = SERIAL_LOG.read_text(errors="replace")
    tail = data[data.rfind("entering main event loop"):]
    if "-> Shell" not in tail:
        raise SystemExit("FAIL: click did not land in the Shell window:\n" + tail[-500:])
    print("Shell window focused")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="C:/Program Files/qemu/qemu-system-i386.exe")
    parser.add_argument("--persist-check", action="store_true",
                        help="boot and verify TEST.TXT (written by a previous "
                        "run) is still readable: notepad :open/:list over serial")
    args = parser.parse_args()

    if SERIAL_LOG.exists():
        SERIAL_LOG.unlink()
    if SCREENSHOT.exists():
        SCREENSHOT.unlink()

    print("booting QEMU (headless)...")
    proc = subprocess.Popen(
        [
            args.qemu,
            "-machine", "pc",
            "-drive", f"file={IMAGE},format=raw,if=ide",
            "-boot", "order=c",
            "-display", "none",
            "-serial", f"file:{SERIAL_LOG}",
            "-monitor", f"tcp:127.0.0.1:{MONITOR_PORT},server,nowait",
            "-no-reboot", "-no-shutdown",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_serial("[TUI] entering main event loop")
        print("kernel booted, TUI up")
        mon = Monitor(MONITOR_PORT)

        focus_shell(mon)
        type_string(mon, "notepad")
        press_enter(mon)
        wait_serial("np> ")
        print("notepad running")

        if args.persist_check:
            type_string(mon, ":open test.txt")
            press_enter(mon)
            time.sleep(0.5)
            type_string(mon, ":list")
            press_enter(mon)
            data = wait_serial("1: " + NOTE_TEXT)
            print("PASS: after reboot notepad reads back %r" % (NOTE_TEXT + "\n"))
            type_string(mon, ":quit")
            press_enter(mon)
            wait_serial("[ELF] user program exited")
            return 0

        # Type one text line, save it, quit.
        type_string(mon, NOTE_TEXT)
        press_enter(mon)
        time.sleep(0.3)

        type_string(mon, ":save test.txt")
        press_enter(mon)
        wait_serial("* saved")
        print("notepad saved TEST.TXT")

        type_string(mon, ":list")
        press_enter(mon)
        time.sleep(0.5)

        type_string(mon, ":quit")
        press_enter(mon)
        wait_serial("[ELF] user program exited")
        print("notepad exited, kernel resumed")

        # Back in the TUI shell: cat the file and take a screenshot as the
        # human-visible proof (the authoritative check is the disk below).
        type_string(mon, "cat test.txt")
        press_enter(mon)
        time.sleep(1.0)
        mon.cmd(f"screendump {SCREENSHOT}")
        time.sleep(0.5)

        # Shut QEMU down gracefully so its host-side writeback cache
        # flushes before we inspect pasinux.img.
        mon.cmd("quit")
        deadline = time.time() + 15
        while proc.poll() is None and time.time() < deadline:
            time.sleep(0.2)
        if proc.poll() is None:
            proc.kill()

        verify_image_content()
        print("ALL CHECKS PASSED")
        return 0
    finally:
        time.sleep(0.2)
        if proc.poll() is None:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
