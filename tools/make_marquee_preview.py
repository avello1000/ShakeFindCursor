# tools/make_marquee_preview.py — 用程序同款算法渲染跑马灯效果示意图
#
# 为什么不直接用屏幕差值截图：差值图里会留下任务栏图标轮廓、时钟等
# 「两帧之间有变化」的真实桌面痕迹，属于隐私信息。本脚本按 main.cpp 中
# Marquee 的同款数学（色相环绕 + 流动亮斑 + 灯丝白芯 + 高斯衰减 + 四角
# over 合成）离线渲染，输出完全不包含任何屏幕内容的纯算法效果图。
#
# 用法：python tools/make_marquee_preview.py [输出路径]
import math, struct, zlib, sys

# ---- 与 main.cpp namespace Config 对齐的参数 ----
HUE_BASE, HUE_SPAN = 0.52, 1.00
SAT, VAL           = 0.80, 1.00
CORE_PX, CORE_WHITE= 3, 0.80
GLOW_DECAY         = 0.28
THICK_RATIO, T_MIN, T_MAX = 0.030, 18, 96
WAVE_NUM, WAVE_AMP = 3.0, 0.26
PHASE, WAVE_PHASE  = 0.00, 0.35        # 示意用的固定相位
W, H               = 1280, 720         # 输出尺寸
BG                 = (16, 16, 24)      # 深色"桌面"底色

T = int(min(W, H) * THICK_RATIO + 0.5)
T = max(T_MIN, min(T_MAX, T))
L = 2.0 * (W + H) - 4.0                # 真实周长（与 Layout() 一致）

def hsv(h):
    hp = (h % 1.0) * 6.0
    sext, f = int(hp), hp - int(hp)
    p, q, t = VAL*(1-SAT), VAL*(1-SAT*f), VAL*(1-SAT*(1-f))
    return {0:(VAL,t,p), 1:(q,VAL,p), 2:(p,VAL,t),
            3:(p,q,VAL), 4:(t,p,VAL), 5:(VAL,p,q)}[sext % 6]

# 垂直剖面：强度 + 掺白（与 BuildProfile 一致）
prof, white = [], []
core = min(CORE_PX, T)
for j in range(T):
    a = 1.0 if j < core else max(0.0, math.exp(-(j-core)/(T*GLOW_DECAY)))
    prof.append(a)
    t = 1.0 - j/(core+4) if j < core+4 else 0.0
    white.append(t*t*CORE_WHITE)

# 画布：BGRA
img = bytearray()
for _ in range(W*H):
    img += bytes((BG[2], BG[1], BG[0], 255))

def composite_strip(x0, y0, along_x, length, s_at0, s_step):
    """单条光带按 over 合成画到画布上（premultiplied over）。"""
    s = s_at0
    for i in range(length):
        hue = (HUE_BASE + PHASE + s / L * HUE_SPAN) % 1.0
        r0, g0, b0 = (c*255 for c in hsv(hue))
        wv = (s / L) * WAVE_NUM + WAVE_PHASE
        e = 1.0 - WAVE_AMP * (0.5 - 0.5*math.sin(wv*2*math.pi))
        for p in range(T):
            a = e * prof[p]
            if a < 0.008: break
            wm = white[p]
            r = r0 + (255-r0)*wm; g = g0 + (255-g0)*wm; b = b0 + (255-b0)*wm
            if along_x:
                x, y = x0+i, y0+p
            else:
                x, y = x0+p, y0+i
            if not (0 <= x < W and 0 <= y < H): continue
            k = (y*W + x) * 4
            da = img[k+3] / 255.0
            sa = a
            img[k+0] = int((b*sa + img[k+0]*(1-sa)))
            img[k+1] = int((g*sa + img[k+1]*(1-sa)))
            img[k+2] = int((r*sa + img[k+2]*(1-sa)))
            img[k+3] = int(255 * (sa + da*(1-sa)))
        s += s_step

# 四条光带：贯通整边 + 弧长精确衔接（同 Layout()）
composite_strip(0, 0,           True,  W, 0.0,            +1)   # 上
composite_strip(W-T, 0,         False, H, W-1,            +1)   # 右
composite_strip(0, H-T,         True,  W, 2*W+H-3,        -1)   # 下
composite_strip(0, 0,           False, H, 2*W+2*H-4,      -1)   # 左

# ---- 编码 PNG（标准库手写，color type 2 = RGB）----
raw = b''
for y in range(H):
    row = img[y*W*4:(y+1)*W*4]
    line = bytearray(b'\x00')                          # filter 0
    for x in range(W):
        i = x*4
        line += bytes((row[i+2], row[i+1], row[i]))    # BGRA → RGB
    raw += bytes(line)

def chunk(typ, data):
    c = struct.pack('>I', len(data)) + typ + data
    return c + struct.pack('>I', zlib.crc32(typ + data) & 0xffffffff)

png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 9))
png += chunk(b'IEND', b'')

out = sys.argv[1] if len(sys.argv) > 1 else 'docs/screenshot-marquee.png'
open(out, 'wb').write(png)
print(f'已生成 {out}（{W}x{H}, 光带厚度 {T}px）')
