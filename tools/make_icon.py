# _make_icon.py — 生成 ShakeFindCursor 的应用图标 app.ico
# 渲染与 main.cpp 中 kArrow 完全一致的指针箭头：白色填充 + 深灰描边 + 柔和投影。
# 超采样抗锯齿后缩到 16/32/48/256 四档，打包成 ICO（纯标准库，无第三方依赖）。
import struct, math

ARROW = [(0.000,0.000),(0.000,0.625),(0.156,0.469),(0.281,0.781),
         (0.406,0.719),(0.281,0.406),(0.500,0.406)]  # 与 main.cpp kArrow 一致

def bbox(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)

def inside(x, y, poly):
    c = False; n = len(poly)
    for i in range(n):
        ax, ay = poly[i]; bx, by = poly[(i+1) % n]
        if (ay > y) != (by > y):
            t = (y - ay) / (by - ay)
            if x < ax + t * (bx - ax):
                c = not c
    return c

def dist_to_poly(x, y, poly):
    best = 1e18; n = len(poly)
    for i in range(n):
        ax, ay = poly[i]; bx, by = poly[(i+1) % n]
        vx, vy = bx - ax, by - ay
        wx, wy = x - ax, y - ay
        L = vx*vx + vy*vy
        t = 0.0 if L == 0 else max(0.0, min(1.0, (wx*vx + wy*vy) / L))
        dx = x - (ax + t*vx); dy = y - (ay + t*vy)
        d = dx*dx + dy*dy
        if d < best: best = d
    return math.sqrt(best)

def render(size, hi=1024):
    """在 hi×hi 超采样画布上渲染后缩到 size×size，返回 BGRA 像素（top-down 行序）。"""
    W = hi
    x0, y0, x1, y1 = bbox(ARROW)
    bw, bh = x1 - x0, y1 - y0
    pad = 0.10                      # 四周留边
    s  = (1.0 - 2*pad) / bh
    cx = 0.5 - (x0 + x1) / 2 * s    # 外接框水平居中
    cy = pad - y0 * s

    poly   = [((px*s + cx)*W, (py*s + cy)*W) for px, py in ARROW]
    shadow = [(px + W*0.030, py + W*0.030) for px, py in poly]
    ow     = W * 0.050              # 描边宽度（超采样像素）

    # 只遍历图形外接框，省掉四角大片透明区
    gx0 = max(0, int(min(px for px, _ in poly + shadow) - ow - 1))
    gy0 = max(0, int(min(py for _, py in poly + shadow) - ow - 1))
    gx1 = min(W, int(max(px for px, _ in poly + shadow) + ow + 2))
    gy1 = min(W, int(max(py for _, py in poly + shadow) + ow + 2))

    # hi 分辨率单通道累加：0=透明, 1=阴影, 2=白色填充, 3=描边
    layer = bytearray(W*W)
    for y in range(gy0, gy1):
        base = y * W
        for x in range(gx0, gx1):
            px = x + 0.5; py = y + 0.5
            if dist_to_poly(px, py, poly) <= ow/2:
                layer[base + x] = 3          # 描边最上层
            elif inside(px, py, poly):
                layer[base + x] = 2
            elif inside(px, py, shadow):
                layer[base + x] = 1

    COLORS = {1: (0, 0, 0, 70), 2: (255, 255, 255, 255), 3: (32, 32, 32, 255)}
    # 盒式降采样 hi→size
    f = W // size
    out = bytearray(size*size*4)
    for Y in range(size):
        for X in range(size):
            r = g = b = a = 0
            for sy in range(f):
                row = (Y*f + sy) * W
                for sx in range(f):
                    v = COLORS.get(layer[row + X*f + sx])
                    if v:
                        r += v[0]; g += v[1]; b += v[2]; a += v[3]
            n = f*f
            i = (Y*size + X) * 4
            out[i+0] = b // n; out[i+1] = g // n; out[i+2] = r // n; out[i+3] = a // n
    return out

def encode_ico(images):
    # images: [(size, BGRA top-down 像素)]
    head = struct.pack('<HHH', 0, 1, len(images))
    entries = b''; blobs = b''; off = 6 + 16*len(images)
    for size, px in images:
        w = 0 if size >= 256 else size
        and_stride = ((size + 31) // 32) * 4
        and_size = and_stride * size
        bmp_header = struct.pack('<IiiHHIIiiII', 40, size, size*2, 1, 32, 0,
                                 size*size*4 + and_size, 0, 0, 0, 0)
        xor = bytearray()
        for y in range(size-1, -1, -1):            # ICO 的位图是 bottom-up
            row = px[y*size*4:(y+1)*size*4]
            for x in range(size):
                i = x*4
                xor += bytes((row[i], row[i+1], row[i+2], row[i+3]))  # RGBA→BGRA
        blob = bmp_header + bytes(xor) + b'\x00' * and_size   # AND 掩码全 0：透明由 alpha 决定
        entries += struct.pack('<BBBBHHII', w, w, 0, 0, 1, 32, len(blob), off)
        blobs += blob
        off += len(blob)
    return head + entries + blobs

imgs = [(s, render(s)) for s in (16, 32, 48, 256)]
open('app.ico', 'wb').write(encode_ico(imgs))
print('app.ico 生成完成:', ['%dpx' % s for s, _ in imgs])
