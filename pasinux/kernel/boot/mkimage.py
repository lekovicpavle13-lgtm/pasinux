#!/usr/bin/env python3
"""Build a bootable FAT12 1.44 MB floppy image for pasinux.

Layout produced (all matches the constants in boot/boot.asm):

    LBA 0        : boot.bin (512-byte FAT12 boot sector, 0xAA55)
    LBA 1 ..   9 : FAT #1 (9 sectors)
    LBA 10 .. 18 : FAT #2 (mirror)
    LBA 19 .. 32 : root directory (224 entries * 32 bytes = 14 sectors)
    LBA 33 ..    : data area; KERNEL.BIN lives here as a 1-cluster-per-sector
                   chain starting at cluster 2.

The PE kernel (kernel.pe) is flattened to a load image first, then written as
the file KERNEL.BIN on the volume so the boot loader can find it by name in the
root directory and walk its FAT chain.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# --- FAT12 / 1.44 MB geometry (DON'T drift from boot.asm) -----------------
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 1
NUM_FATS = 2
ROOT_ENTRIES = 224
TOTAL_SECTORS = 2880
SECTORS_PER_FAT = 9
MEDIA = 0xF0

ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32) // BYTES_PER_SECTOR  # 14
ROOT_DIR_LBA = RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT)  # 19
DATA_LBA = ROOT_DIR_LBA + ROOT_DIR_SECTORS  # 33

# Total cluster count that fits in the data area.
TOTAL_CLUSTERS = TOTAL_SECTORS - RESERVED_SECTORS - NUM_FATS * SECTORS_PER_FAT - ROOT_DIR_SECTORS
# Kernel cluster numbering starts at 2.
KERNEL_CLUSTER = 2


def pe_load_sections(pe: bytes, keep: set[str]) -> tuple[int, bytearray]:
    """Flatten a PE into a raw, position-correct image (from the existing tool)."""
    if pe[:2] != b"MZ":
        raise SystemExit("kernel.pe: not a PE image")

    e_lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    _machine, nsec, _ts, _sptr, _sn, optsize, _chars = struct.unpack_from(
        "<HHIIIHH", pe, e_lfanew + 4
    )
    opt_off = e_lfanew + 24
    magic = struct.unpack_from("<H", pe, opt_off)[0]
    if magic != 0x10B:
        raise SystemExit(f"kernel.pe: expected PE32, got magic {magic:#x}")

    image_base = struct.unpack_from("<I", pe, opt_off + 28)[0]
    sec_off = opt_off + optsize

    chunks: list[tuple[str, int, int, int, int]] = []
    for i in range(nsec):
        off = sec_off + i * 40
        name = pe[off : off + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", pe, off + 8)
        if name not in keep:
            continue
        chunks.append((name, vsize, vaddr, rsize, roff))

    if not chunks:
        raise SystemExit("kernel.pe: no loadable sections found")

    base_vma = min(vaddr for _, _, vaddr, _, _ in chunks)
    end = 0
    for _, vsize, vaddr, _, _ in chunks:
        off = vaddr - base_vma
        end = max(end, off + vsize)

    image = bytearray(end)
    for name, vsize, vaddr, rsize, roff in chunks:
        n = min(rsize, vsize)
        off = vaddr - base_vma
        image[off : off + n] = pe[roff : roff + n]

    return image_base, image


def make_boot_sector(boot: bytes) -> bytes:
    """Validate the boot sector and return it padded to one sector."""
    if len(boot) != BYTES_PER_SECTOR:
        raise SystemExit(f"boot.bin must be {BYTES_PER_SECTOR} bytes, got {len(boot)}")
    if boot[510:512] != b"\x55\xaa":
        raise SystemExit("boot.bin missing 0xAA55 signature")
    return boot


def fat12_entry(index: int) -> int:
    """Byte offset of the FAT entry for cluster `index` (12-bit packing)."""
    return index + (index >> 1)


def fat12_write(table: bytearray, cluster: int, value: int) -> None:
    """Store `value` (12 bits) for `cluster` in the FAT bytearray."""
    off = fat12_entry(cluster)
    word = table[off] | (table[off + 1] << 8)
    if cluster & 1:  # odd cluster -> high 12 bits
        word = (word & 0x000F) | (value << 4) & 0xFFF0
    else:  # even cluster -> low 12 bits
        word = (word & 0xF000) | (value & 0x0FFF)
    table[off] = word & 0xFF
    table[off + 1] = (word >> 8) & 0xFF


def fat12_cluster_lba(cluster: int) -> int:
    """LBA of the data sector holding `cluster`."""
    return DATA_LBA + (cluster - 2) * SECTORS_PER_CLUSTER


def build_image(boot: bytes, kernel: bytes) -> bytearray:
    """Assemble the full 1.44 MB floppy image."""
    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

    # --- Boot sector (LBA 0) -------------------------------------------------
    image[0 : BYTES_PER_SECTOR] = boot

    # --- FATs -----------------------------------------------------------------
    n_clusters = (len(kernel) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    if KERNEL_CLUSTER + n_clusters - 1 > TOTAL_CLUSTERS + 1:
        # highest usable cluster = TOTAL_CLUSTERS + 1 (clusters start at 2)
        raise SystemExit(
            f"kernel needs {n_clusters} clusters; only "
            f"{TOTAL_CLUSTERS} fit on a 1.44 MB floppy"
        )

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    # Cluster 0: media descriptor byte + 0xFF0 (reserved marker).
    fat12_write(fat, 0, 0xFF0 | (MEDIA & 0x0F))
    fat12_write(fat, 1, 0xFFF)  # cluster 1 reserved -> EOF

    # Chain KERNEL: cluster N -> N+1, last -> 0xFFF (EOF).
    for i in range(n_clusters):
        cluster = KERNEL_CLUSTER + i
        nxt = cluster + 1 if i < n_clusters - 1 else 0xFFF
        fat12_write(fat, cluster, nxt)

    # Write both FAT copies.
    for f in range(NUM_FATS):
        off = (RESERVED_SECTORS + f * SECTORS_PER_FAT) * BYTES_PER_SECTOR
        image[off : off + len(fat)] = fat

    # --- Root directory (LBA 19) ----------------------------------------------
    root_off = ROOT_DIR_LBA * BYTES_PER_SECTOR
    entry = bytearray(32)
    entry[0:8] = b"KERNEL  "                      # 8.3 name (8)
    entry[8:11] = b"BIN"                          # extension (3)
    entry[11] = 0x20                              # archive attribute
    entry[26:28] = struct.pack("<H", KERNEL_CLUSTER)
    entry[28:32] = struct.pack("<I", len(kernel))
    image[root_off : root_off + 32] = entry

    # --- Data area: write kernel clusters at their FAT-specified LBAs ----------
    kernel_data = kernel + b"\0" * ((-len(kernel)) % BYTES_PER_SECTOR)
    for i in range(n_clusters):
        cluster = KERNEL_CLUSTER + i
        off = fat12_cluster_lba(cluster) * BYTES_PER_SECTOR
        chunk = kernel_data[i * BYTES_PER_SECTOR : (i + 1) * BYTES_PER_SECTOR]
        if len(chunk) != BYTES_PER_SECTOR:
            raise SystemExit("internal: cluster chunk wrong size")
        image[off : off + BYTES_PER_SECTOR] = chunk

    return image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pe", type=Path, default=Path("kernel.pe"))
    parser.add_argument("--boot", type=Path, default=Path("boot.bin"))
    parser.add_argument("--kernel-bin", type=Path, default=Path("kernel.bin"))
    parser.add_argument("--image", type=Path, default=Path("pasinux.img"))
    # --kernel-sectors retained for backward compat with the Makefile, but the
    # file now sets its own size; the arg is accepted and ignored.
    parser.add_argument("--kernel-sectors", type=int, default=100,
                        help="(retained for Makefile compat; ignored)")
    args = parser.parse_args()

    boot = make_boot_sector(args.boot.read_bytes())

    image_base, flat = pe_load_sections(
        args.pe.read_bytes(), {".text", ".rdata", ".data"}
    )
    if image_base != 0x10000:
        print(
            f"warning: image base is {image_base:#x}, boot expects 0x10000",
            file=sys.stderr,
        )
    if len(flat) > (TOTAL_CLUSTERS) * BYTES_PER_SECTOR:
        raise SystemExit(
            f"kernel.bin is {len(flat)} bytes; does not fit in "
            f"{TOTAL_CLUSTERS} clusters on the floppy"
        )

    # Write the flattened kernel to kernel.bin (used as a build artifact).
    args.kernel_bin.write_bytes(bytes(flat))
    print(f"wrote {args.kernel_bin} ({len(flat)} bytes)")

    image = build_image(boot, flat)
    args.image.write_bytes(image)
    print(
        f"wrote {args.image}: {TOTAL_SECTORS} sectors, "
        f"{len(flat)}-byte kernel in {1 + (len(flat)-1)//BYTES_PER_SECTOR} clusters "
        f"starting at cluster {KERNEL_CLUSTER}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())