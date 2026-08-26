#!/usr/bin/env python3
"""Analyze QEMU screendumps (PPM) of the pasinux TUI desktop.

VGA text mode is 720x400 -> 80x25 cells of 9x16 px. For each cell we
compute mean R,G,B and classify brightness. Outputs an 80x25 luminance
map so window rectangles/borders are visible, and can hash crops.
"""
import sys, hashlib

CELL_W, CELL_H = 9, 16

def load_ppm(path):
    data = open(path, 'rb').read()
    # parse header: P6\n<w> <h>\n<max>\n
    if not data.startswith(b'P6'):
        raise SystemExit('not P6')
    idx = data.index(b'\n')
    dims = b''
    i = idx + 1
    while len(dims.split()) < 2 or b'\n' not in dims:
        dims += data[i:i+1]; i += 1
    w, h = map(int, dims.split())
    # skip max line
    j = data.index(b'\n', i)
    pix = data[j+1:]
    return w, h, pix

def cell_lum(w, h, pix, crow, ccol):
    x0, y0 = ccol*CELL_W, crow*CELL_H
    tot_r = tot_g = tot_b = n = 0
    for yy in range(y0, min(y0+CELL_H, h)):
        base = (yy*w + x0) * 3
        row = pix[base:base + CELL_W*3]
        for xx in range(0, min(CELL_W, w - x0)):
            r, g, b = row[xx*3], row[xx*3+1], row[xx*3+2]
            tot_r += r; tot_g += g; tot_b += b; n += 1
    return (tot_r//n, tot_g//n, tot_b//n)

def lum_map(path):
    w, h, pix = load_ppm(path)
    lines = []
    for cr in range(h // CELL_H):
        line = ''
        for cc in range(w // CELL_W):
            r, g, b = cell_lum(w, h, pix, cr, cc)
            lum = (r + g + b) // 3
            if lum < 40: ch = '.'
            elif lum < 100: ch = '-'
            elif lum < 180: ch = '+'
            else: ch = '#'
            line += ch
        lines.append(line)
    return '\n'.join(lines)

def crop_hash(path, r0, c0, rows, cols):
    w, h, pix = load_ppm(path)
    x0, y0, x1, y1 = c0*CELL_W, r0*CELL_H, (c0+cols)*CELL_W, (r0+rows)*CELL_H
    blob = bytearray()
    for yy in range(y0, min(y1, h)):
        base = (yy*w + x0) * 3
        blob += pix[base:base + (min(x1, w)-x0)*3]
    return hashlib.sha256(bytes(blob)).hexdigest()[:16]

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'map':
        print(lum_map(sys.argv[2]))
    elif cmd == 'hash':
        print(crop_hash(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])))
