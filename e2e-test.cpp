// 临时端到端测试器（v2：修正光标测量）
//   _e2e width / shake / exit / shakekill
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#include <windows.h>
#include <stdio.h>

// 方法A：当前显示的光标（不可靠——光标可能落在控制台上）
static int WidthOfHandle(HCURSOR c) {
    if (!c) return -99;
    ICONINFO ii = {};
    if (!::GetIconInfo(c, &ii)) return -98;
    int w = -97;
    BITMAP bm = {};
    if (ii.hbmColor && ::GetObject(ii.hbmColor, sizeof(bm), &bm)) w = (int)bm.bmWidth;
    else if (ii.hbmMask && ::GetObject(ii.hbmMask, sizeof(bm), &bm)) w = (int)bm.bmWidth;
    if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  ::DeleteObject(ii.hbmMask);
    return w;
}

// 方法B：直接查系统箭头槽（IDC_ARROW == OCR_NORMAL，就是被 SetSystemCursor 替换的那个）
static int ArrowSlotWidth() {
    HCURSOR a = (HCURSOR)::LoadImageW(nullptr, (LPCWSTR)IDC_ARROW, IMAGE_CURSOR,
                                      0, 0, LR_SHARED);
    return WidthOfHandle(a);
}

static int WidthShown() {
    CURSORINFO ci = { sizeof(CURSORINFO) };
    if (!::GetCursorInfo(&ci)) return -95;
    return WidthOfHandle(ci.hCursor);
}

static void Report(const char* tag) {
    printf("%-14s 显示中=%-5d 箭头槽=%-5d\n", tag, WidthShown(), ArrowSlotWidth());
}

static void DoShake(int cx, int cy) {
    for (int i = 0; i < 8; ++i) {
        ::SetCursorPos(cx + 120, cy);
        ::Sleep(22);
        ::SetCursorPos(cx - 120, cy);
        ::Sleep(22);
    }
}

static HWND FindApp(DWORD* pid) {
    HWND h = ::FindWindowW(L"ShakeFindCursorWnd", L"ShakeFindCursor");
    if (h && pid) ::GetWindowThreadProcessId(h, pid);
    return h;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* mode = (argc > 1) ? argv[1] : L"width";

    if (wcscmp(mode, L"width") == 0) { Report("当前:"); return 0; }

    // 探测跑马灯边条（用于确认它有没有在空闲时偷偷跑）
    if (wcscmp(mode, L"marquee") == 0) {
        printf("显示器数量 = %d\n", ::GetSystemMetrics(SM_CMONITORS));
        int n = 0;
        HWND h2 = nullptr;
        for (;;) {
            h2 = ::FindWindowExW(nullptr, h2, L"ShakeFindCursorMarquee", nullptr);
            if (!h2) break;
            ++n;
            RECT r = {};
            ::GetWindowRect(h2, &r);
            printf("  边条 #%d (%ld,%ld)-(%ld,%ld) 可见=%s\n", n,
                   r.left, r.top, r.right, r.bottom,
                   ::IsWindowVisible(h2) ? "是" : "否");
        }
        printf("跑马灯边条数量 = %d\n", n);
        return 0;
    }

    DWORD pid = 0;
    HWND h = FindApp(&pid);
    if (!h) { printf("没找到窗口\n"); return 1; }
    printf("窗口=%p  PID=%lu\n", (void*)h, (unsigned long)pid);

    POINT old{};
    ::GetCursorPos(&old);

    if (wcscmp(mode, L"shake") == 0) {
        Report("晃动前:");
        DoShake(old.x, old.y);
        Report("晃动后:");
        ::Sleep(700);
        Report("停0.7s后:");
    } else if (wcscmp(mode, L"shakekill") == 0) {
        DoShake(old.x, old.y);
        Report("放大中:");
        ::SetCursorPos(old.x, old.y);
        HANDLE p = ::OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!p) { printf("OpenProcess 失败 %lu\n", ::GetLastError()); return 2; }
        printf("TerminateProcess = %d\n", ::TerminateProcess(p, 137));
        ::CloseHandle(p);
    } else if (wcscmp(mode, L"exit") == 0) {
        DoShake(old.x, old.y);
        Report("放大中:");
        ::PostMessageW(h, WM_COMMAND, 3, 0);
        ::Sleep(1200);
        Report("退出后:");
        printf("窗口还在吗=%s\n", ::IsWindow(h) ? "在" : "已退出");
    }

    ::SetCursorPos(old.x, old.y);
    return 0;
}
