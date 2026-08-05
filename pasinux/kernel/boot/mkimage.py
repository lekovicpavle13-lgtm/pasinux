
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def pe_load_sections(pe: bytes, keep: set[str]) -> tuple[int, bytearray]:
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

    # physical offset = RVA − base_vma
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pe", type=Path, default=Path("kernel.pe"))
    parser.add_argument("--boot", type=Path, default=Path("boot.bin"))
    parser.add_argument("--kernel-bin", type=Path, default=Path("kernel.bin"))
    parser.add_argument("--image", type=Path, default=Path("pasinux.img"))
    parser.add_argument(
        "--kernel-sectors",
        type=int,
        default=100,
        help="sectors reserved for the kernel (must match boot.asm)",
    )
    args = parser.parse_args()

    image_base, flat = pe_load_sections(
        args.pe.read_bytes(), {".text", ".rdata", ".data"}
    )
    if image_base != 0x10000:
        print(
            f"warning: image base is {image_base:#x}, boot expects 0x10000",
            file=sys.stderr,
        )

    kernel_bytes = args.kernel_sectors * 512
    if len(flat) > kernel_bytes:
        raise SystemExit(
            f"kernel.bin is {len(flat)} bytes; exceeds {args.kernel_sectors} sectors"
        )

    padded = bytearray(kernel_bytes)
    padded[: len(flat)] = flat
    args.kernel_bin.write_bytes(padded)

    boot = args.boot.read_bytes()
    if len(boot) != 512:
        raise SystemExit(f"boot.bin must be 512 bytes, got {len(boot)}")
    if boot[510:512] != b"\x55\xaa":
        raise SystemExit("boot.bin missing 0xAA55 signature")

    args.image.write_bytes(boot + padded)
    print(
        f"wrote {args.kernel_bin} ({len(flat)} used, {kernel_bytes} padded) "
        f"and {args.image} ({512 + kernel_bytes} bytes)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
