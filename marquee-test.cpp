// marquee-test.cpp — 多屏跑马灯的视觉验证工具
//
// 用途：合成本次晃动，抓「晃动前 / 跑马灯显示中 / 淡出后」三张屏，输出**差值图**。
//       差值图里只剩变化的部分（也就是跑马灯），桌面内容自动抵消 —— 既不泄露画面，
//       又能一眼看清跑马灯的形状、位置、颜色。
//       同时逐边取样统计，确认四条边都在渲染、淡出后无残留。
//
// 编译：
//   cl /nologo /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE marquee-test.cpp ^
//      /link user32.lib gdi32.lib gdiplus.lib /SUBSYSTEM:CONSOLE /OUT:marquee-test.exe
// 运行（需主程序已在跑）：
//   marquee-test.exe
//
// ⚠️ 踩过的坑：本工具**必须**声明 per-monitor DPI aware。
//    早期版本是 DPI 未感知的，而主屏是 200% 缩放 —— 系统会给未感知进程一份
//    "缩放且错位"的桌面副本，于是多屏坐标全乱：明明渲染正常的右边条被测成
//    "整条 0 变化"，害我白查了一轮。改 DPI 感知后立刻正常。
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#include <windows.h>
#include <objidl.h>          // GDI+ 依赖 IStream / PROPID，必须先于 gdiplus.h
#include <gdiplus.h>
#include <stdio.h>
#include <vector>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")

struct Shot {
    int x = 0, y = 0, w = 0, h = 0;   // 真实像素
    std::vector<unsigned char> px;    // BGRA
};

static void MakeDpiAware() {
    typedef BOOL (WINAPI *PFN)(HANDLE);
    HMODULE u = ::GetModuleHandleW(L"user32.dll");
    PFN fn = u ? (PFN)::GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
    if (fn) fn((HANDLE)(INT_PTR)-4);   // PER_MONITOR_AWARE_V2
    else    ::SetProcessDPIAware();
}

static bool Grab(Shot& s, const RECT& r) {
    s.x = r.left; s.y = r.top;
    s.w = r.right - r.left;
    s.h = r.bottom - r.top;
    if (s.w <= 0 || s.h <= 0) return false;

    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = s.w;
    bi.bmiHeader.biHeight      = -s.h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { ::DeleteDC(mem); ::ReleaseDC(nullptr, screen); return false; }
    HGDIOBJ old = ::SelectObject(mem, dib);

    ::BitBlt(mem, 0, 0, s.w, s.h, screen, s.x, s.y, SRCCOPY | CAPTUREBLT);

    s.px.assign((size_t)s.w * s.h * 4, 0);
    ::memcpy(s.px.data(), bits, s.px.size());

    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return true;
}

static int DiffAt(const Shot& a, const Shot& b, int x, int y) {
    if (x < 0 || y < 0 || x >= a.w || y >= a.h) return 0;
    const size_t i = ((size_t)y * a.w + x) * 4;
    int d = 0;
    for (int c = 0; c < 3; ++c) {
        const int v = std::abs((int)a.px[i + c] - (int)b.px[i + c]);
        if (v > d) d = v;
    }
    return d;
}

// 差值图（放大后保存）。BGR 逐通道求差，所以**颜色是保留的** ——
// 能直接看出跑马灯到底是什么配色。桌面内容自动抵消，不泄露画面。
static void SaveDiff(const Shot& a, const Shot& b, int amp, const wchar_t* path) {
    Gdiplus::Bitmap out(a.w, a.h, PixelFormat32bppARGB);
    {
        Gdiplus::BitmapData bd;
        Gdiplus::Rect rc(0, 0, a.w, a.h);
        out.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd);
        unsigned char* dst = (unsigned char*)bd.Scan0;
        for (int y = 0; y < a.h; ++y) {
            unsigned char* row = dst + (size_t)y * bd.Stride;
            for (int x = 0; x < a.w; ++x) {
                const size_t i = ((size_t)y * a.w + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    int v = std::abs((int)a.px[i + c] - (int)b.px[i + c]) * amp;
                    if (v > 255) v = 255;
                    row[x * 4 + c] = (unsigned char)v;   // B,G,R 顺序一致，直接搬
                }
                row[x * 4 + 3] = 255;
            }
        }
        out.UnlockBits(&bd);
    }
    CLSID clsid = {};
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    std::vector<unsigned char> buf(sz);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(n, sz, info);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(info[i].MimeType, L"image/png") == 0) { clsid = info[i].Clsid; break; }
    out.Save(path, &clsid, nullptr);
}

static void DoShake(int cx, int cy) {
    for (int i = 0; i < 8; ++i) {
        ::SetCursorPos(cx + 120, cy);
        ::Sleep(22);
        ::SetCursorPos(cx - 120, cy);
        ::Sleep(22);
    }
}

static void ProbeMarquee() {
    int n = 0;
    HWND h = nullptr;
    for (;;) {
        h = ::FindWindowExW(nullptr, h, L"ShakeFindCursorMarquee", nullptr);
        if (!h) break;
        ++n;
        RECT r = {};
        ::GetWindowRect(h, &r);
        printf("  边条 #%d: (%ld,%ld)-(%ld,%ld) %ldx%ld 可见=%s\n", n,
               r.left, r.top, r.right, r.bottom,
               r.right - r.left, r.bottom - r.top,
               ::IsWindowVisible(h) ? "是" : "否");
    }
    if (!n) printf("  >>> 没有找到任何跑马灯边条 <<<\n");
}

// 沿边缘路径穿过一个角，采样差值，报告相邻采样点的最大跳变。
// 平滑的羽化过渡 => 相邻跳变小；有断口 => 在接缝处出现接近满幅的跳变。
//
// inset 说明：扫描走【辉光区】（距屏幕边缘 inset 像素，饱和色区域），
// 不要贴着最外圈 —— 最外几像素是掺白的"灯丝"芯线，浅色核心在白色任务栏上
// 逐通道差值天然偏小，会把平滑的亮度过渡误报成"断口"（实测踩过）。
struct CornerScan { int maxStep = 0; int maxD = 0; };

static CornerScan ScanCorner(const Shot& a, const Shot& b,
                             int bw, int bh, int corner, int inset, int D) {
    const int pts[4][2] = { {bw-1-inset, inset}, {bw-1-inset, bh-1-inset},
                            {inset, bh-1-inset}, {inset, inset} };
    const int cx = pts[corner][0], cy = pts[corner][1];

    CornerScan r;
    int prev = -1;
    for (int k = -D; k <= D; k += 4) {
        int x, y;
        switch (corner) {
            case 0:  // 右上：沿顶边从左逼近 → 沿右边下行
                if (k <= 0) { x = cx + k; y = cy; } else { x = cx; y = cy + k; }
                break;
            case 1:  // 右下：沿右边上行逼近 → 沿下边向左离开
                if (k <= 0) { x = cx; y = cy + k; } else { x = cx - k; y = cy; }
                break;
            case 2:  // 左下：沿下边从右逼近 → 沿左边上行
                if (k <= 0) { x = cx - k; y = cy; } else { x = cx; y = cy - k; }
                break;
            default: // 左上：沿左边下行逼近 → 沿顶边向右离开
                if (k <= 0) { x = cx; y = cy - k; } else { x = cx + k; y = cy; }
                break;
        }
        if (x < 0) x = 0; if (x >= bw) x = bw - 1;
        if (y < 0) y = 0; if (y >= bh) y = bh - 1;
        const int d = DiffAt(a, b, x, y);
        if (d > r.maxD) r.maxD = d;
        if (prev >= 0) { int stp = d - prev; if (stp < 0) stp = -stp; if (stp > r.maxStep) r.maxStep = stp; }
        prev = d;
    }
    return r;
}

int wmain() {
    MakeDpiAware();

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR tok = 0;
    Gdiplus::GdiplusStartup(&tok, &gsi, nullptr);

    printf("显示器数量 = %d（真实像素）\n", ::GetSystemMetrics(SM_CMONITORS));
    printf("虚拟屏 = (%d,%d) %dx%d\n",
           ::GetSystemMetrics(SM_XVIRTUALSCREEN), ::GetSystemMetrics(SM_YVIRTUALSCREEN),
           ::GetSystemMetrics(SM_CXVIRTUALSCREEN), ::GetSystemMetrics(SM_CYVIRTUALSCREEN));

    HWND h = ::FindWindowW(L"ShakeFindCursorWnd", L"ShakeFindCursor");
    if (!h) { printf("主程序没在跑\n"); return 1; }

    POINT cur{};
    ::GetCursorPos(&cur);
    HMONITOR mon = ::MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    ::GetMonitorInfoW(mon, &mi);
    const RECT R = mi.rcMonitor;
    printf("光标所在显示器 = (%ld,%ld)-(%ld,%ld)  %ldx%ld\n",
           R.left, R.top, R.right, R.bottom, R.right - R.left, R.bottom - R.top);

    const POINT anchor = { (R.left + R.right) / 2, (R.top + R.bottom) / 2 };

    Shot before, ctrlShot, during, after;
    ::SetCursorPos(anchor.x, anchor.y);
    ::Sleep(200);
    Grab(before, R);
    ::Sleep(150);
    Grab(ctrlShot, R);          // 静止对照：什么都不做再抓一张，用来测噪声底

    DoShake(anchor.x, anchor.y);
    ::Sleep(250);
    Grab(during, R);
    printf("\n晃动后探测跑马灯窗口：\n");
    ProbeMarquee();

    ::Sleep(1800);
    ::SetCursorPos(anchor.x, anchor.y);
    ::Sleep(250);
    Grab(after, R);

    // 逐边扫描（坐标都是显示器内的相对坐标）
    const int bw = R.right - R.left, bh = R.bottom - R.top;
    struct { const char* name; bool horiz; int x, y, w, h; } edges[4] = {
        {"上边", true,  0,          0,          bw, 40},
        {"下边", true,  0,          bh - 40,    bw, 40},
        {"左边", false, 0,          40,         40, bh - 80},
        {"右边", false, bw - 40,    40,         40, bh - 80},
    };
    printf("\n逐边扫描（每条边取 12 点，测 during-before 与 after-before）：\n");
    for (const auto& e : edges) {
        int hit = 0, noiseMax = 0;
        char d1[256] = {}, d2[256] = {};
        for (int k = 1; k <= 12; ++k) {
            const int x = e.horiz ? (e.x + e.w * k / 13) : (e.x + 20);
            const int y = e.horiz ? (e.y + 20)           : (e.y + e.h * k / 13);
            if (DiffAt(before, during, x, y) > 25) ++hit;
            char t[16];
            sprintf_s(t, "%3d ", DiffAt(before, during, x, y)); strcat_s(d1, t);
            sprintf_s(t, "%3d ", DiffAt(before, after,  x, y)); strcat_s(d2, t);
            const int nd = DiffAt(before, ctrlShot, x, y);
            if (nd > noiseMax) noiseMax = nd;
        }
        printf("  %s: 命中 %d/12（静止噪声底 max=%d）\n      显示中=[%s]\n      淡出后=[%s]\n",
               e.name, hit, noiseMax, d1, d2);
    }

    // 四角平滑度：沿辉光区（内缩 24px）的边界路径穿过每个角，
    // 看相邻采样点的最大跳变。D=150 能覆盖旧版接缝（距角约一个光带厚度）。
    printf("\n四角平滑度扫描（辉光区 inset=24px，步进4px，±150px）：\n");
    const char* names[4] = { "右上", "右下", "左下", "左上" };
    int worst = 0;
    for (int c = 0; c < 4; ++c) {
        const CornerScan cs = ScanCorner(before, during, bw, bh, c, 24, 150);
        const CornerScan nz = ScanCorner(before, ctrlShot, bw, bh, c, 24, 150);
        if (cs.maxStep > worst) worst = cs.maxStep;
        printf("  %s角: 峰值差=%3d  最大相邻跳变=%3d（噪声底=%d）%s\n",
               names[c], cs.maxD, cs.maxStep, nz.maxStep,
               cs.maxStep <= (nz.maxStep + 25) ? "OK 平滑" : "!! 有断口");
    }
    printf("  ==> 四角最差相邻跳变 = %d（< 26 视为羽化自然）\n", worst);

    SaveDiff(before, during, 6, L"_marquee-crop.png");
    printf("\n已保存 _marquee-crop.png（%dx%d）\n", bw, bh);

    Gdiplus::GdiplusShutdown(tok);
    return 0;
}
