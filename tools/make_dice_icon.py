"""Generate 78x20 1-bit indexed LVGL v8 .bin menu icons (two dice) for each theme."""
import struct, os
W, H = 78, 20
# Foreground colour per theme as (B, G, R), sampled from each theme's level.bin icon
COLORS = {1:(0,101,255), 2:(0,206,253), 3:(255,255,255), 4:(68,43,38), 5:(46,24,29),
          6:(0,255,0), 7:(55,238,247), 8:(0,193,94), 9:(0,209,255)}

def die(px, x0, y0, s, face):
    corners = {(0,0),(s-1,0),(0,s-1),(s-1,s-1)}
    for y in range(s):
        for x in range(s):
            if (x,y) not in corners:
                px[y0+y][x0+x] = 1
    m, c, f = 4, s//2, s-5   # pip centres for s=18
    pos = {'TL':(m,m),'TR':(f,m),'ML':(m,c),'MR':(f,c),'BL':(m,f),'BR':(f,f),'C':(c,c)}
    faces = {1:['C'],2:['TL','BR'],3:['TL','C','BR'],4:['TL','TR','BL','BR'],
             5:['TL','TR','C','BL','BR'],6:['TL','ML','BL','TR','MR','BR']}
    for name in faces[face]:
        cx, cy = pos[name]
        for dy in (-1,0,1):
            for dx in (-1,0,1):
                px[y0+cy+dy][x0+cx+dx] = 0

px = [[0]*W for _ in range(H)]
die(px, 19, 1, 18, 5)
die(px, 41, 1, 18, 3)

stride = (W + 7) // 8
rows = bytearray()
for y in range(H):
    row = bytearray(stride)
    for x in range(W):
        if px[y][x]:
            row[x // 8] |= 0x80 >> (x % 8)
    rows += row

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "spiffs_image")
for t, (b, g, r) in COLORS.items():
    header = struct.pack("<I", 7 | (W << 10) | (H << 21))   # cf=INDEXED_1BIT
    palette = bytes([0,0,0,0, b,g,r,255])
    path = f"{root}/theme{t}/menu/dice.bin"
    with open(path, "wb") as fh:
        fh.write(header + palette + rows)
    print(path, os.path.getsize(path))
# preview
for row in px: print("".join("#" if v else "." for v in row))
