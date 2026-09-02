"""Generate 78x20 1-bit indexed LVGL v8 .bin menu icons (an analog clock) for each theme."""
import struct, os, math
W, H = 78, 20
COLORS = {1:(0,101,255), 2:(0,206,253), 3:(255,255,255), 4:(68,43,38), 5:(46,24,29),
          6:(0,255,0), 7:(55,238,247), 8:(0,193,94), 9:(0,209,255)}

px = [[0]*W for _ in range(H)]
cx, cy, r = 39, 9.5, 9.0
for y in range(H):
    for x in range(W):
        d = math.hypot(x - cx, y - cy)
        if r - 1.6 <= d <= r + 0.4:            # ring
            px[y][x] = 1
for x in range(35, 44):                        # hour hand (3 o'clock)
    px[9][x] = px[10][x] = 1
for y in range(3, 11):                         # minute hand (12 o'clock)
    px[y][39] = px[y][38] = 1
px[9][39] = px[10][39] = px[9][38] = px[10][38] = 1  # hub

stride = (W + 7) // 8
rows = bytearray()
for y in range(H):
    row = bytearray(stride)
    for x in range(W):
        if px[y][x]:
            row[x // 8] |= 0x80 >> (x % 8)
    rows += row

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "spiffs_image")
for t, (b, g, r_) in COLORS.items():
    header = struct.pack("<I", 7 | (W << 10) | (H << 21))
    palette = bytes([0,0,0,0, b,g,r_,255])
    path = os.path.join(root, f"theme{t}", "menu", "clock.bin")
    with open(path, "wb") as fh:
        fh.write(header + palette + rows)
for row in px: print("".join("#" if v else "." for v in row)[25:55])
