// ShakeFindCursor — 仿 macOS「摇动鼠标放大指针」的 Windows 工具
//
// 快速晃动鼠标时，指针平滑放大，方便快速定位；手停下来后自动平滑缩回。
// 放大图形用 GDI+ 抗锯齿矢量绘制（不是位图缩放，所以不模糊），
// 经 CreateIconIndirect 生成 HCURSOR 后 SetSystemCursor 全局替换。
// 不需要管理员权限。
//
// ---------------------------------------------------------------------------
// v4.7 跑马灯重做为「Siri 风格 RGB 环绕流光」
// ---------------------------------------------------------------------------
// 需求：接了两个及以上显示器时，晃动触发放大后，在【光标所在那块屏】的四边亮起
//       类似 iPhone 唤醒 Siri 那种 RGB 彩色环绕光效；单显示器时不出现。
// 上一版是"一圈很淡的常亮描边 + 一个单色光点绕屏一周"—— 太暗、太窄、只有一种颜色，
// 观感差。这一版重做为：
//   · 整圈色相渐变：hue = 基准 + 流动相位 + (弧长/周长) × 色环跨度。
//     颜色沿边缘自然过渡成一条完整彩虹，并且整条彩虹随时间绕屏流动。
//   · 流动亮斑：沿边叠加若干个正弦亮斑（与色相用不同速度），整圈不是死板的等亮彩虹。
//   · 近白"灯丝"亮心：光带最里几行向白色过渡，做出"发光体"而不是"彩色胶带"的质感。
//   · 更宽更亮：厚度按屏幕短边取 2.8%（1920 高 → 54px），核心不透明度接近满值。
//   · 辉光走指数衰减（σ = 厚度 × 0.22），衰减到几乎为 0 的行直接 break；
//     颜色只沿【沿边】方向变化，所以每列只算一次颜色，垂直方向一路乘 alpha 写下去。
// 结构（沿用 v4.6）：
//   · 每条边一个 WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW
//     的穿透置顶窗口，用 UpdateLayeredWindow 做逐像素 alpha 合成（光晕才平滑）。
//     为什么不整屏开一个窗口？1920×1080 的 32bpp 位图每帧要合成 ~8MB，
//     而四条光带合计只有约 1.5MB。
//   · 四条光带【贯通整条边】并在四角重叠：分层窗口的 over 合成近似"取并集"，
//     水平辉光与垂直辉光在角落互相补齐 —— 相当于一根弯过角落的霓虹管，
//     沿边缘强度处处连续，没有接缝断口；弧长参数在四个角点精确相等，
//     色相与亮斑相位过角零跳变，彩虹是真的"绕"过角落。
//   · 跑马灯按 50fps 跑，**不跟刷新率** —— 它是慢速流光，50fps 足够且更省资源。
//   · 进程声明为 per-monitor DPI aware，保证多屏不同缩放时坐标与像素 1:1，光带不糊。
// ---------------------------------------------------------------------------
// v4.6 多屏跑马灯（初版，已被 v4.7 取代）
// v4.5 动画按刷新率锁定 + 降低资源占用
// ---------------------------------------------------------------------------
// 一、动画帧率对齐显示器刷新率
//   旧做法：固定 10ms 的 WM_TIMER 定时器，帧间隔既和刷新率无关，又受系统
//           时钟节拍（默认 ~15.6ms）限制，抖动明显 → 看着"一顿一顿"。
//   新做法：QPC 时间基准 + 高精度可等待定时器(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
//           + MsgWaitForMultipleObjectsEx 驱动主循环。帧间隔 = 1/刷新率
//           （60Hz→16.67ms，120Hz→8.33ms，144Hz→6.94ms），并落在绝对时间栅格上，
//           不累积漂移；某帧画慢了会自动跳帧而不是拖慢后续。
//           刷新率按【光标所在显示器】查询，换屏/改分辨率(WM_DISPLAYCHANGE)自动重取。
// 二、缓动改为 easeInOutCubic
//   两端速度为 0，起步不窜、收尾不急刹。旧版放大用 easeOutCubic，第一帧就从
//   23px 跳到 ~43px，观感突兀。
// 三、资源占用
//   (1) 去掉 timeBeginPeriod(1) —— 这是最重的一项：它把【整个系统】的时钟分辨率
//       抬到 1ms，阻止 CPU 进入深度睡眠，持续耗电。高精度可等待定时器不需要它。
//   (2) 轮询间隔 8ms → 16ms（与系统时钟节拍对齐，不产生额外唤醒）。
//   (3) 静止快速通道：光标位置没变就直接跳过整套运动学判定，空闲时几乎零开销。
//   (4) 复用内存 DC 建 DIB；尺寸没变时不重绘不重装光标。
//   → 空闲时每秒只做 ~62 次 GetCursorPos + 一次坐标比较；动画期间才按刷新率出帧，
//     且只在"整数尺寸真的变了"时才真正重绘+SetSystemCursor。
// ---------------------------------------------------------------------------
// v4.4 光标还原兜底（修复"退出后鼠标还是大的"）
// ---------------------------------------------------------------------------
// 现象：程序异常终止（被任务管理器强杀 / 崩溃 / 调试器停止）时，WM_DESTROY 里的
//       还原代码没有机会执行，SetSystemCursor 装进系统的放大光标就永久留下了。
// 实测确认：优雅退出（托盘"退出"）的还原路径本身是好的——32px→96px→32px 全程正确，
//       只有"进程被强杀"这一种情况会留下残留。
// 对策（三层）：
//   1) 看门狗子进程：主进程启动时 spawn 一个自身副本 `--watchdog <pid>`，
//      它只做一件事——等待父进程结束（无论正常退出还是被强杀），然后还原系统光标。
//      这是唯一能覆盖"被 TerminateProcess 强杀"的手段。
//   2) 崩溃/注销兜底：SetUnhandledExceptionFilter（崩溃）+ WM_QUERYENDSESSION /
//      WM_ENDSESSION（注销、关机）。
//   3) 一键还原：`ShakeFindCursor.exe --restore` 无条件执行 SPI_SETCURSORS。
//      另附 restore-cursor.bat 便于双击。
// 另：实测 SetSystemCursor 不需要管理员权限（非提权进程同样生效），
//     因此构建脚本已去掉 requireAdministrator，启动不再弹 UAC。
// ---------------------------------------------------------------------------
// v4.3 灵敏度对齐 macOS（"Shake mouse pointer to locate"）
// ---------------------------------------------------------------------------
// 问题：上一版"一动就放大"。两个原因——
//   (1) 直线滑动通道阈值太松：kSwipeSpeed=1150px/s、kSwipeMinPx=80px，
//       正常把鼠标从屏幕这头划到那头（约 1250px/s）就超阈值了。
//       → macOS 根本没有这条通道，它只认"来回摆动"。故默认关闭，保留为可选开关。
//   (2) 晃动判定太毛糙：在 8ms 采样上数"相邻两段位移的符号翻转"，死区只有 2px，
//       手部细微抖动就能凑出 3 次"反转"，于是随手一划也可能命中。
//       → 改成【转折点式摆动分析】：必须从极值点反向回撤 >= kShakeMinAmpPx(24px)
//         才计一次有效摆动；再叠加"净位移/路程 <= 0.45（必须是来回摆，不是长距离拖动）"
//         与速度门槛，普通移动/手抖都无法命中。
// 效果：单方向移动（无论多快）永不触发；只有真正快速来回晃若干次才会放大。
// ---------------------------------------------------------------------------
// 1) 两条【独立】触发通道，满足任一即放大（原来是「速度 AND 方向翻转」的与关系，
//    导致快速直线滑动因翻转次数为 0 而永不触发）：
//      A. 快速直线滑动 —— 单位时间净位移超阈值 且 轨迹足够直（默认关闭，见 Config::kEnableSwipe）
//      B. 快速左右晃动 —— 有效摆动次数达标 且 近期速度快 且 是"来回摆"而非拖动
// 2) 修复「第一次放大后再次晃动失效」：
//    MSDN 明确说明 SetSystemCursor() 会用 DestroyCursor 销毁传入的 hcur。
//    原代码把 HCURSOR 缓存到 g_bigCursor 复用，第二次传入的已是【失效句柄】，
//    调用静默失败、看上去就是"晃不动"。现改为每次重新创建、绝不复用。
// 3) 方向反转判定改为【转折点式】：从极值反向回撤达最小振幅才算一次摆动，
//   x / y 两轴分别统计取最大值，斜向/画圈晃动同样能识别，且天然免疫手抖。
// 4) 恢复逻辑由"固定 0.8s 后瞬间恢复"改为"速度回落到平缓阈值并持续一小段
//    时间后开始回缩"，且放大/回缩都走【缓动动画】，逐帧重绘光标位图，
//    过渡自然平滑、无突兀跳变。
// 5) 顺带修掉位图创建里的 use-after-ReleaseDC（原代码 ReleaseDC 后又用
//    该 HDC 调 SetDIBits），改用 CreateDIBSection 直接绘制，免去 GDI+/DIB 来回拷贝。

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // GetTickCount64 等需要 Vista+ 声明
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE          // 使 OCR_NORMAL 等系统光标资源 ID 可用
#include <windows.h>
#include <objidl.h>          // GDI+ 头依赖 IStream / PROPID
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <deque>
#include <vector>
#include <cmath>
#include <cstring>           // memset
#include <cstdlib>           // wcstoul

// 高精度可等待定时器标志（Win10 1803+；老 SDK 里没有就自己补一个）
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// ===========================================================================
// 可调参数（想改手感，基本只需要动这里）
// ===========================================================================
namespace Config {

    // ---------------- 采样 ----------------
    // 16ms 与系统默认时钟节拍(~15.6ms)基本对齐，既够用又不会额外增加唤醒次数。
    // 不再依赖 timeBeginPeriod —— 那样会把全系统时钟分辨率抬到 1ms，很费电。
    const int      kPollMs         = 16;     // 位移采样间隔(ms)
    const int      kHistoryMs      = 760;    // 轨迹保留时长(ms)，需 >= 最长的判定窗口
    const size_t   kHistoryMax     = 200;    // 轨迹采样上限（防极端情况下膨胀）

    // ---------------- 触发通道 A：快速直线滑动 ----------------
    // macOS 没有这个机制（它只认来回摆动）。打开后"快速甩一下鼠标"就会放大，
    // 日常用鼠标几乎必然误触发，所以默认关闭。真要开，阈值也调得很苛刻：
    const bool     kEnableSwipe    = false;  // ← 想恢复"快速滑动也放大"就改成 true
    const double   kSwipeWindowSec = 0.14;   // 判定窗口(秒)
    const double   kSwipeSpeed     = 1800.0; // 净位移速度阈值(px/s)，比旧版 1150 高得多
    const double   kSwipeMinPx     = 160.0;  // 窗口内最小净位移(px)
    const double   kSwipeStraight  = 0.85;   // 直线度 = 净位移 / 路径长度，1.0 为绝对笔直

    // ---------------- 触发通道 B：快速晃动（对齐 macOS 手感）----------------
    const double   kShakeWindowSec = 0.55;   // 数"有效摆动次数"的时间窗(秒)
    const double   kShakeMinAmpPx  = 24.0;   // 一次摆动至少要反向回撤这么多像素才算数
    const int      kShakeReversals = 3;      // 窗口内至少几次有效摆动（≈ 来回晃两下）
    const double   kShakeSpeedSec  = 0.30;   // 看"当前速度"的时间窗(秒)
    const double   kShakeSpeed     = 550.0;  // 速度下限(px/s)，够快才算"晃"
    const double   kShakeMaxNetRatio = 0.45; // 净位移/路程 上限：必须是"来回摆"，不是长距离拖动

    // ---------------- 恢复（平缓判定） ----------------
    const double   kCalmWindowSec  = 0.22;   // 测"当前速度"用的窗口(秒)，取长一点以免摆动转折点误判为停下
    const double   kCalmSpeed      = 150.0;  // 低于此速度视为"平缓"(px/s)
    const int      kCalmDelayMs    = 180;    // 持续平缓多久开始回缩(ms)
    const int      kMinHoldMs      = 300;    // 每次触发后至少放大这么久(ms)，避免闪烁

    // ---------------- 缩放动画 ----------------
    const float    kScaleMin       = 0.72f;  // 空闲时的等效尺度：此时箭头视觉大小≈系统原生箭头
    const float    kScaleMax       = 3.0f;   // 放大倍数（位图边长 = kNativePx * 该值）
    const int      kNativePx       = 32;     // 系统光标标准边长(px)，运行时会按 SM_CXCURSOR 校正
    const int      kGrowMs         = 180;    // 放大动画时长(ms)
    const int      kShrinkMs       = 300;    // 回缩动画时长(ms)
    const int      kAnimMinFps     = 24;     // 动画帧率下限（刷新率读到异常值时的兜底）
    const int      kAnimMaxFps     = 240;    // 动画帧率上限（刷新率再高也不超过，省资源）

    // ---------------- 多屏跑马灯：Siri 风格 RGB 环绕流光 ----------------
    // 接了两个及以上显示器时，放大期间在【光标所在屏】四边亮起一圈**环绕流动的
    // RGB 彩色光晕**（整圈色相渐变 + 流动亮斑 + 近白"灯丝"亮心），一眼就能确认
    // 指针在哪块屏。单显示器时完全不出现。
    const bool     kMarqueeEnable    = true; // ← 总开关（托盘菜单也能切）
    const double   kMarqueeThickRatio= 0.030;// 光带厚度 = min(屏宽,屏高) × 该比例
    const int      kMarqueeThickMin  = 18;   // 厚度下限(px)
    const int      kMarqueeThickMax  = 96;   // 厚度上限(px)
    const int      kMarqueeCorePx    = 3;    // 最亮的"灯丝"核心线宽度(px)
    const double   kMarqueeGlowDecay = 0.28; // 辉光衰减：σ = 厚度 × 该值（越小光带越"紧"）
    const double   kMarqueeSat       = 0.80; // 颜色饱和度（1.0 最艳；太高会显得廉价）
    const double   kMarqueeVal       = 1.00; // 颜色明度
    const double   kMarqueeCoreWhite = 0.80; // 核心线掺白比例，做出"灯丝"亮心
    const double   kMarqueeHueSpan   = 1.00; // 用掉多少色环：1.0=整圈彩虹；≈0.42≈蓝→紫→粉→橙
    const double   kMarqueeHueBase   = 0.52; // 色环起点
    const double   kMarqueeFlowSec   = 2.6;  // 色相绕屏流一圈的时间(秒)
    const double   kMarqueeWaveNum   = 3.0;  // 一圈上有几个流动亮斑
    const double   kMarqueeWaveSec   = 3.1;  // 亮斑流动周期(秒)
    const double   kMarqueeWaveAmp   = 0.26; // 亮斑明暗起伏幅度 0~1
    const double   kMarqueeFadeSec   = 0.25; // 淡入 / 淡出时长(秒)
    const int      kMarqueeFps       = 50;   // 跑马灯帧率（慢速流光，不必跟刷新率）
}

// ===========================================================================
// 箭头几何：经典指针外形，归一化坐标（尖端在 (0,0)，主体向右下延伸）
// ===========================================================================
static const float kArrow[7][2] = {
    {0.000f, 0.000f},   // 尖端
    {0.000f, 0.625f},   // 左侧边到底
    {0.156f, 0.469f},   // 内凹（箭头与尾巴的交界）
    {0.281f, 0.781f},   // 尾巴左下
    {0.406f, 0.719f},   // 尾巴右下
    {0.281f, 0.406f},   // 尾巴右上
    {0.500f, 0.406f},   // 头部右下
};
static const float kArrowW   = 0.500f;              // 归一化包围盒宽
static const float kArrowH   = 0.781f;              // 归一化包围盒高
static const float kArrowPad = 0.05f;               // 位图内边距（留空间给描边/投影）

// 把归一化坐标映射到 [0,1] 位图坐标所用的缩放系数
static inline float ArrowScale() { return (1.0f - 2.0f * kArrowPad) / kArrowH; }

// ===========================================================================
// 全局状态
// ===========================================================================
static HINSTANCE g_hInst    = nullptr;
static HWND      g_hwnd     = nullptr;
static bool      g_enabled  = true;
static bool      g_shutdown = false;

// 轨迹采样
struct Sample { POINT pt; ULONGLONG when; };
static std::deque<Sample> g_history;

// 缩放状态机
static float g_scale       = Config::kScaleMin;  // 当前尺度
static float g_target      = Config::kScaleMin;  // 目标尺度
static float g_animFrom    = Config::kScaleMin;  // 本段动画起点
static float g_animTo      = Config::kScaleMin;  // 本段动画终点
static bool  g_animActive  = false;              // 是否正在出帧
static bool  g_enlarged    = false;              // 是否处于"放大"状态
static ULONGLONG g_lastTrigger = 0;               // 最近一次触发时刻
static ULONGLONG g_lastActive  = 0;               // 最近一次"运动超过平缓阈值"的时刻
static int   g_nativePx    = Config::kNativePx;  // 运行时校准的原生光标边长
static int   g_installedPx = 0;                  // 当前已装进系统的自定义光标边长；0 = 未接管

// 动画时序：QPC 时间基准 + 高精度可等待定时器（不需要 timeBeginPeriod）
static HANDLE        g_animTimer   = nullptr;
static LARGE_INTEGER g_qpf         = {};         // QPC 频率
static double        g_animT0      = 0.0;        // 本段动画起点（QPC 秒）
static double        g_animDurSec  = 0.0;        // 本段动画时长（秒）
static double        g_frameSec    = 1.0 / 60.0; // 帧间隔 = 1/刷新率
static double        g_nextFrameAt = 0.0;        // 下一帧的 QPC 时刻（绝对栅格，不累积漂移）

// 光标所在显示器的刷新率缓存
static HMONITOR g_mon       = nullptr;
static int      g_refreshHz = 60;

// 静止快速通道：位置没变就跳过整套判定
static POINT g_lastPt = { INT_MIN, INT_MIN };

// 复用一个内存 DC 来建 DIB（省掉每帧 GetDC/ReleaseDC）
static HDC g_memDC = nullptr;

// 托盘
static const UINT WM_TRAYICON = WM_APP + 1;
static NOTIFYICONDATAW g_nid = {};
static bool g_trayAdded = false;

static const wchar_t kAppName[] = L"ShakeFindCursor";
static const wchar_t kRunKey[]  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// ===========================================================================
// 前置声明
// ===========================================================================
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void Tick();
static void AnimStep();
static void ForceRestore();
static HCURSOR MakePointerCursor(int side);
static std::wstring GetExePath();

// ===========================================================================
// 轨迹与运动学统计
// ===========================================================================
static void RecordMove(POINT pt, ULONGLONG now) {
    g_history.push_back({pt, now});

    // 按时间裁剪
    while (!g_history.empty() &&
           (now - g_history.front().when) > (DWORD)Config::kHistoryMs) {
        g_history.pop_front();
    }
    // 按数量裁剪（双保险）
    while (g_history.size() > Config::kHistoryMax) {
        g_history.pop_front();
    }
}

// 一段时间窗内的运动特征
struct Motion {
    double path      = 0.0;   // 沿轨迹累计路程(px)
    double net       = 0.0;   // 首尾净位移(px)
    double dt        = 0.0;   // 时间跨度(s)
    double speed     = 0.0;   // path / dt，反映"动得多快"
    double netSpeed  = 0.0;   // net  / dt，反映"实际挪了多远"
    int    reversals = 0;     // 主轴上方向反转次数
    bool   valid     = false;
};

// 单轴"摆动"计数：只有在极值点反向回撤 >= amp 像素，才记一次有效摆动。
// 这样零星的手部微颤（几像素来回）永远凑不出次数，而真正的来回晃能稳定计数。
static int CountOscillations(const double* v, size_t n, double amp) {
    if (n < 2) return 0;

    int    reversals = 0;
    int    dir = 0;          // 0=方向未定, +1=向上摆, -1=向下摆
    double hi = v[0], lo = v[0];   // dir==0 时同时跟踪上下极值
    double extreme = v[0];         // dir!=0 时跟踪当前方向上的极值

    for (size_t i = 1; i < n; ++i) {
        const double x = v[i];
        if (dir == 0) {
            if (x > hi) hi = x;
            if (x < lo) lo = x;
            if (x - lo >= amp)      { dir = +1; extreme = x; }   // 从低点向上拉开 → 定为向上摆
            else if (hi - x >= amp) { dir = -1; extreme = x; }   // 从高点向下拉开 → 定为向下摆
        } else if (dir > 0) {
            if (x > extreme) extreme = x;
            else if (extreme - x >= amp) { ++reversals; dir = -1; extreme = x; }  // 到顶后回撤够多
        } else {
            if (x < extreme) extreme = x;
            else if (x - extreme >= amp) { ++reversals; dir = +1; extreme = x; }  // 到底后回升够多
        }
    }
    return reversals;
}

static Motion WindowStats(double winSec) {
    Motion m;
    const size_t n = g_history.size();
    if (n < 3) return m;

    const ULONGLONG now = g_history.back().when;
    const DWORD span = (DWORD)(winSec * 1000.0 + 0.5);

    // 从末尾往前找窗口内的第一个样本
    size_t i0 = n - 1;
    while (i0 > 0 && (now - g_history[i0 - 1].when) <= span) --i0;
    if (n - i0 < 3) return m;

    // 路程 + 两轴投影（投影用于后面的摆动分析）
    double xs[Config::kHistoryMax];
    double ys[Config::kHistoryMax];
    size_t cnt = 0;
    POINT prev = g_history[i0].pt;
    xs[cnt] = (double)prev.x;
    ys[cnt] = (double)prev.y;
    ++cnt;

    for (size_t i = i0 + 1; i < n && cnt < Config::kHistoryMax; ++i) {
        const long dx = g_history[i].pt.x - prev.x;
        const long dy = g_history[i].pt.y - prev.y;
        m.path += std::sqrt((double)dx * (double)dx + (double)dy * (double)dy);
        xs[cnt] = (double)g_history[i].pt.x;
        ys[cnt] = (double)g_history[i].pt.y;
        ++cnt;
        prev = g_history[i].pt;
    }

    // x / y 两轴分别数摆动次数，取较大者 → 左右晃、上下晃、斜向晃都能识别
    const int revX = CountOscillations(xs, cnt, Config::kShakeMinAmpPx);
    const int revY = CountOscillations(ys, cnt, Config::kShakeMinAmpPx);
    m.reversals = (revX > revY) ? revX : revY;

    const POINT& a = g_history[i0].pt;
    const POINT& b = g_history[n - 1].pt;
    m.net = std::sqrt((double)(b.x - a.x) * (double)(b.x - a.x) +
                      (double)(b.y - a.y) * (double)(b.y - a.y));

    m.dt = (double)(now - g_history[i0].when) / 1000.0;
    if (m.dt <= 0.0) return m;

    m.speed    = m.path / m.dt;
    m.netSpeed = m.net  / m.dt;
    m.valid    = true;
    return m;
}

// 通道 A：快速直线滑动（默认关闭，见 Config::kEnableSwipe）
static bool DetectSwipe() {
    const Motion m = WindowStats(Config::kSwipeWindowSec);
    if (!m.valid) return false;
    if (m.net < Config::kSwipeMinPx)      return false;  // 位移太小，忽略
    if (m.netSpeed < Config::kSwipeSpeed) return false;  // 单位时间位移不够快
    if (m.path <= 1.0) return false;
    return (m.net / m.path) >= Config::kSwipeStraight;   // 轨迹要够"直"，避免把晃动误判成滑动
}

// 通道 B：快速晃动 —— 对应 macOS "Shake mouse pointer to locate"
static bool DetectShake() {
    const Motion r = WindowStats(Config::kShakeWindowSec);
    if (!r.valid) return false;
    if (r.reversals < Config::kShakeReversals) return false;   // 摆动次数不够
    if (r.path <= 1.0) return false;
    // 必须是"来回摆"而不是"朝一个方向拖"：晃动时净位移远小于累计路程
    if (r.net / r.path > Config::kShakeMaxNetRatio) return false;

    // 速度用更短的窗口看，避免被摆动转折处的低速段稀释
    const Motion s = WindowStats(Config::kShakeSpeedSec);
    return s.valid && s.speed >= Config::kShakeSpeed;
}

// ===========================================================================
// GDI+ 绘制高清指针箭头（白色填充 + 深灰描边 + 半透明投影）
// ===========================================================================
static void DrawPointerBitmap(Gdiplus::Bitmap& bmp, int side) {
    using namespace Gdiplus;

    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.Clear(Color(0, 0, 0, 0));

    const float S = (float)side;
    const float s = ArrowScale();

    // 归一化顶点 → 位图坐标
    PointF pts[7];
    for (int i = 0; i < 7; ++i) {
        pts[i].X = kArrowPad + kArrow[i][0] * s;
        pts[i].Y = kArrowPad + kArrow[i][1] * s;
        pts[i].X *= S;
        pts[i].Y *= S;
    }

    GraphicsPath path;
    path.StartFigure();
    for (int i = 0; i < 7; ++i)
        path.AddLine(pts[i], pts[(i + 1) % 7]);
    path.CloseFigure();

    // ---- 投影：同一路径向右下偏移后描粗 ----
    {
        GraphicsPath shadow;
        shadow.AddPath(&path, FALSE);
        Matrix m;
        m.Translate(S * 0.028f, S * 0.028f);
        shadow.Transform(&m);

        SolidBrush sb(Color(70, 0, 0, 0));
        Pen pen(&sb, S * 0.055f);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, &shadow);
    }

    // ---- 主体：白填充 + 深灰描边 ----
    {
        SolidBrush white(Color(255, 255, 255, 255));
        Pen outline(Color(255, 32, 32, 32), S * 0.042f);
        outline.SetLineJoin(LineJoinRound);
        g.FillPath(&white, &path);
        g.DrawPath(&outline, &path);
    }
}

// ---------------------------------------------------------------------------
// 已渲染位图缓存
// ---------------------------------------------------------------------------
// 实测每帧重画一次矢量箭头约 0.86ms，占了整个动画开销的一半。
// HCURSOR 不能缓存（SetSystemCursor 成功后会把句柄销毁），但能缓存
// ICONINFO 里那两张【位图】—— 因为 CreateIconIndirect 会把它们复制一份。
// 于是：位图按边长缓存，每帧只重建句柄，省掉重复的 GDI+ 绘制与掩码扫描。
struct CursorBits { int side; HBITMAP color; HBITMAP mask; };
static std::vector<CursorBits> g_bitsCache;

// 渲染一张指定边长的箭头位图（颜色 DIB + 单色掩码）
static bool RenderPointerBits(int side, HBITMAP& outColor, HBITMAP& outMask) {
    outColor = nullptr;
    outMask  = nullptr;
    if (side < 16) side = 16;

    // 复用常驻内存 DC（每次建/还 DC 是白开销）；万一还没建好就临时取一个
    const bool ownDC = (g_memDC == nullptr);
    HDC hdc = ownDC ? ::GetDC(nullptr) : g_memDC;
    if (!hdc) return false;

    // 32bpp 顶向下 DIB，带 alpha —— 可直接当作 CreateIconIndirect 的颜色位图
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(BITMAPV5HEADER);
    bi.bV5Width       = side;
    bi.bV5Height      = -side;              // 负高度 = 顶向下，与 GDI+ 扫描行方向一致
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void* bits = nullptr;
    HBITMAP color = ::CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                       &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) ::DeleteObject(color);
        if (ownDC) ::ReleaseDC(nullptr, hdc);
        return false;
    }

    // GDI+ 直接画进这段 DIB 内存（在 color 失效前完成）
    {
        Gdiplus::Bitmap bmp(side, side, side * 4, PixelFormat32bppARGB,
                            (BYTE*)bits);
        if (bmp.GetLastStatus() == Gdiplus::Ok)
            DrawPointerBitmap(bmp, side);
    }

    // 单色掩码：不透明像素对应 bit = 0（Windows 光标约定），其余为 1
    // 单色 DDB 每行按 16 位（2 字节）对齐
    const int maskStride = ((side + 15) / 16) * 2;
    std::vector<unsigned char> maskBits((size_t)maskStride * (size_t)side, 0xFF);
    {
        const BYTE* px = (const BYTE*)bits;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const BYTE a = px[(size_t)y * side * 4 + (size_t)x * 4 + 3];
                if (a >= 24) {   // 略有像素即算不透明，避免细描边被裁掉
                    maskBits[(size_t)y * maskStride + (size_t)(x / 8)] &=
                        (unsigned char)~(0x80u >> (x & 7));
                }
            }
        }
    }
    HBITMAP mask = ::CreateBitmap(side, side, 1, 1, maskBits.data());
    if (!mask) {
        ::DeleteObject(color);
        if (ownDC) ::ReleaseDC(nullptr, hdc);
        return false;
    }

    if (ownDC) ::ReleaseDC(nullptr, hdc);
    outColor = color;
    outMask  = mask;
    return true;
}

// 取（必要时渲染并缓存）指定位长的位图
static const CursorBits* GetCursorBits(int side) {
    for (const CursorBits& b : g_bitsCache)
        if (b.side == side) return &b;

    CursorBits nb = {};
    if (!RenderPointerBits(side, nb.color, nb.mask)) return nullptr;
    nb.side = side;
    g_bitsCache.push_back(nb);
    return &g_bitsCache.back();
}

static void FreeCursorBits() {
    for (const CursorBits& b : g_bitsCache) {
        if (b.color) ::DeleteObject(b.color);
        if (b.mask)  ::DeleteObject(b.mask);
    }
    g_bitsCache.clear();
}

// 生成一枚带 alpha 的系统光标；失败返回 nullptr。
// 每次都必须新建 —— SetSystemCursor 成功后会把句柄销毁（MSDN），不能复用。
static HCURSOR MakePointerCursor(int side) {
    if (side < 16) side = 16;

    const CursorBits* b = GetCursorBits(side);
    if (!b) return nullptr;
    const HBITMAP color = b->color;      // 先拷出来，避免 vector 扩容后指针悬垂
    const HBITMAP mask  = b->mask;

    // 热点 = 箭头尖端在位图中的位置，保证点击位置不漂移
    const int hot = (int)(kArrowPad * side + 0.5f);

    ICONINFO ii = {};
    ii.fIcon     = FALSE;
    ii.xHotspot  = (DWORD)hot;
    ii.yHotspot  = (DWORD)hot;
    ii.hbmColor  = color;
    ii.hbmMask   = mask;
    return ::CreateIconIndirect(&ii);    // 内部会复制位图
}

// ===========================================================================
// 光标安装 / 恢复
// ===========================================================================
// 把系统箭头替换成指定尺度下的自定义光标。
// 注意：SetSystemCursor 成功后会用 DestroyCursor 销毁我们传入的句柄（MSDN），
//       所以每次都必须新建，绝不能缓存复用；失败时我们自己释放以免泄漏。
static void ApplyScale(float scale) {
    int px = (int)((float)g_nativePx * scale + 0.5f);   // 按系统实际光标尺寸换算
    if (px < 16) px = 16;
    if (px == g_installedPx) return;     // 尺寸没变，不必重装

    HCURSOR cur = MakePointerCursor(px);
    if (!cur) return;

    if (::SetSystemCursor(cur, OCR_NORMAL)) {
        g_installedPx = px;              // 系统已接管并销毁 cur
    } else {
        ::DestroyCursor(cur);            // 失败：自己回收
    }
}

// 无条件交还系统原生光标（可重复调用，幂等）
static void ForceSystemCursors() {
    ::SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    g_installedPx = 0;
}

// 交还系统原生光标（仅在确实接管过时才调用）
static void RestoreSystemCursor() {
    if (g_installedPx != 0 || g_enlarged) ForceSystemCursors();
}

// ===========================================================================
// 时间基准 / 显示器刷新率
// ===========================================================================
static double QpcNow() {
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)g_qpf.QuadPart;
}

// 查指定显示器当前刷新率（Hz）；失败返回 0
static int QueryRefreshHz(HMONITOR mon) {
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(mon, &mi)) return 0;

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (::EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        const int hz = (int)dm.dmDisplayFrequency;
        if (hz > 1) return hz;      // 0/1 表示"硬件默认"，得走下面那条路
    }
    HDC dc = ::CreateDCW(mi.szDevice, nullptr, nullptr, nullptr);
    if (!dc) return 0;
    const int hz = ::GetDeviceCaps(dc, VREFRESH);
    ::DeleteDC(dc);
    return hz;
}

// 光标换到别的显示器时重新取刷新率（同一显示器则直接返回，开销可忽略）
// 返回 true 表示光标所在显示器发生了变化
static bool EnsureRefreshRate(POINT pt) {
    HMONITOR mon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    if (mon == g_mon) return false;
    g_mon = mon;

    int hz = QueryRefreshHz(mon);
    if (hz < Config::kAnimMinFps || hz > Config::kAnimMaxFps) hz = 60;
    g_refreshHz = hz;
    g_frameSec  = 1.0 / (double)hz;
    return true;
}

// ===========================================================================
// 缓动 + 动画状态机
// ===========================================================================
// easeInOutCubic：两端速度为 0。起步不"窜"、收尾不"急刹"，
// 而且它是整数尺寸逐帧变化，视觉上最接近"自然地长大/缩小"。
static float EaseInOutCubic(float t) {
    if (t < 0.5f) return 4.0f * t * t * t;
    const float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) * 0.5f;
}

// 推进到下一帧的栅格点。
// 用【绝对时间栅格】g_animT0 + n*帧间隔，所以不会累积漂移；
// 如果某一帧画得太慢已经错过了后续格点，就直接跳到下一个未来格点（丢帧而不是拖慢）。
// 注意：这里不碰定时器 —— 定时器由主循环统一按「最近到期时刻」装填，
//       免得轮询与出帧两套时间源互相覆盖。
static void AnimAdvanceFrameGrid() {
    const double now = QpcNow();
    do { g_nextFrameAt += g_frameSec; } while (g_nextFrameAt <= now);
}

// 停止出帧（交还定时器，之后线程完全阻塞，不占 CPU）
static void StopAnim() {
    g_animActive = false;
    if (g_animTimer) ::CancelWaitableTimer(g_animTimer);
}

// 启动 / 重启一段动画。缩放中途被打断（抖动续命、回缩中再次触发）也能平滑接上：
// from 传当前实际尺度，于是不会有跳变。
static void StartAnim(float from, float to, double durSec) {
    g_animActive  = true;
    g_animFrom    = from;
    g_animTo      = to;
    g_animDurSec  = (durSec > 0.0) ? durSec : 0.001;
    g_animT0      = QpcNow();
    g_nextFrameAt = g_animT0;
    AnimStep();                                   // 立刻画第一帧，避免拖到下一格
}

// 出一帧
static void AnimStep() {
    if (!g_animActive) return;

    const double now = QpcNow();
    double u = (now - g_animT0) / g_animDurSec;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;

    const float e = EaseInOutCubic((float)u);
    g_scale = g_animFrom + (g_animTo - g_animFrom) * e;

    // 回缩收尾：不再多画一帧自定义小箭头，直接把系统光标交还回去，
    // 避免"自定义箭头 → 系统箭头"的形态跳变。
    if (u >= 1.0 && g_animTo <= Config::kScaleMin + 1e-3f) {
        g_scale    = Config::kScaleMin;
        g_target   = Config::kScaleMin;
        g_enlarged = false;
        StopAnim();
        RestoreSystemCursor();
        return;
    }

    ApplyScale(g_scale);      // 内部会跳过"整数尺寸没变"的帧，不白干活

    if (u >= 1.0) { StopAnim(); return; }
    AnimAdvanceFrameGrid();
}

// ===========================================================================
// 多屏跑马灯
// ===========================================================================
namespace Marquee {

const int kStrips = 4;
const wchar_t kClass[] = L"ShakeFindCursorMarquee";

struct Strip {
    HWND           hwnd     = nullptr;
    int            x = 0, y = 0, w = 0, h = 0;  // 屏幕坐标 + 位图尺寸
    bool           alongIsX = true;             // 沿边轴：true=x(上下边)，false=y(左右边)
    // 垂直剖面在缓冲区里的方向。DIB 是【顶向下】存储：
    //   上边条 缓冲第 0 行 = 屏幕顶边 = 外沿 → 不翻转
    //   左边条 缓冲第 0 列 = 屏幕左边 = 外沿 → 不翻转
    //   下边条 缓冲第 0 行 = 内沿（屏幕底边才是最后一行）→ 要翻转
    //   右边条 缓冲第 0 列 = 内沿 → 要翻转
    bool           perpFlip = false;
    int            thick    = 0;
    int            alongLen = 0;
    double         sAt0     = 0.0;              // 沿边索引 0 处的弧长
    double         sStep    = 1.0;              // 每步弧长增量（+1 / -1）
    HDC            memDC    = nullptr;
    HBITMAP        dib      = nullptr;
    HGDIOBJ        oldBmp   = nullptr;
    unsigned char* px       = nullptr;          // BGRA，已预乘
};

static Strip g_strip[kStrips];

// 色环查表：1024 档 hue -> RGB，避免每列都做一次 HSV 换算
static unsigned char g_hueLut[1024 * 3] = {};
// 垂直方向的衰减与"掺白"档位（长度 = 光带厚度）
static std::vector<unsigned char> g_prof;      // 辉光强度 0..255
static std::vector<unsigned char> g_whiteMix;  // 掺白比例 0..255（做出灯丝亮心）
static bool g_tablesReady = false;

static bool   g_built      = false;
static bool   g_on         = false;           // 期望显示（淡出期间仍为 false）
static float  g_alpha      = 0.0f;            // 当前淡入淡出系数
static double g_phase      = 0.0;             // 色相流动相位 0..1
static double g_wavePhase  = 0.0;             // 亮斑流动相位 0..1
static double g_perimeter  = 1.0;
static double g_lastAt     = 0.0;
static HMONITOR g_builtFor = nullptr;         // 已按哪块屏布好局
static int    g_monCount   = 1;               // 显示器数量（>=2 才启用）
static bool   g_userOn     = Config::kMarqueeEnable;   // 托盘菜单可切的总开关

static const double kPi = 3.14159265358979323846;

// ---- 色环查表：H(0..1) → RGB ----
static void BuildHueLut() {
    const double S = Config::kMarqueeSat;
    const double V = Config::kMarqueeVal;
    for (int i = 0; i < 1024; ++i) {
        const double h  = (double)i / 1024.0;
        const double hp = h * 6.0;
        const int    sext = (int)hp;
        const double f = hp - sext;
        const double p = V * (1.0 - S);
        const double q = V * (1.0 - S * f);
        const double t = V * (1.0 - S * (1.0 - f));
        double r, g, b;
        switch (sext % 6) {
            case 0:  r = V; g = t; b = p; break;
            case 1:  r = q; g = V; b = p; break;
            case 2:  r = p; g = V; b = t; break;
            case 3:  r = p; g = q; b = V; break;
            case 4:  r = t; g = p; b = V; break;
            default: r = V; g = p; b = q; break;
        }
        g_hueLut[i * 3 + 0] = (unsigned char)(r * 255.0 + 0.5);
        g_hueLut[i * 3 + 1] = (unsigned char)(g * 255.0 + 0.5);
        g_hueLut[i * 3 + 2] = (unsigned char)(b * 255.0 + 0.5);
    }
}

// ---- 垂直剖面：核心几行满强度 + 指数辉光衰减 + 核心掺白 ----
static void BuildProfile(int thick) {
    if (thick < 4) thick = 4;
    g_prof.assign(thick, 0);
    g_whiteMix.assign(thick, 0);

    const int    core  = (Config::kMarqueeCorePx < thick) ? Config::kMarqueeCorePx : 1;
    const double sigma = (double)thick * Config::kMarqueeGlowDecay;
    const double sg    = (sigma > 1.0) ? sigma : 1.0;

    for (int j = 0; j < thick; ++j) {
        double a = 1.0;
        if (j >= core) {
            a = std::exp(-(double)(j - core) / sg);
            if (a < 0.004) a = 0.0;          // 尾部直接归零，渲染时可以提前 break
        }
        g_prof[j] = (unsigned char)(a * 255.0 + 0.5);

        // 核心线掺白：前几行二次衰减，做出"灯丝"亮心
        double wm = 0.0;
        const int whiteRows = core + 4;
        if (j < whiteRows) {
            const double t = 1.0 - (double)j / (double)whiteRows;
            wm = t * t * Config::kMarqueeCoreWhite;
        }
        g_whiteMix[j] = (unsigned char)(wm * 255.0 + 0.5);
    }
}

static void BuildTables(int thick) {
    if (!g_tablesReady) { BuildHueLut(); g_tablesReady = true; }
    BuildProfile(thick);
}

static bool EnsureClass() {
    static int state = 0;                 // 0=未尝试 1=成功 2=失败
    if (state == 1) return true;
    if (state == 2) return false;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ::DefWindowProcW;   // 纯画布，不需要处理任何消息
    wc.hInstance     = g_hInst;
    wc.lpszClassName = kClass;
    if (::RegisterClassExW(&wc)) { state = 1; return true; }
    if (::GetLastError() == ERROR_CLASS_ALREADY_EXISTS) { state = 1; return true; }
    state = 2;
    return false;
}

static void ReleaseStrip(Strip& st) {
    if (st.memDC) {
        if (st.oldBmp) ::SelectObject(st.memDC, st.oldBmp);
        ::DeleteDC(st.memDC);
        st.memDC = nullptr; st.oldBmp = nullptr;
    }
    if (st.dib) { ::DeleteObject(st.dib); st.dib = nullptr; }
    if (st.hwnd) { ::DestroyWindow(st.hwnd); st.hwnd = nullptr; }
    st.px = nullptr;
}

static bool CreateStrip(Strip& st) {
    ReleaseStrip(st);
    if (st.w <= 0 || st.h <= 0) return false;

    st.hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClass, L"", WS_POPUP, st.x, st.y, st.w, st.h,
        nullptr, nullptr, g_hInst, nullptr);
    if (!st.hwnd) return false;

    st.memDC = ::CreateCompatibleDC(nullptr);
    if (!st.memDC) return false;

    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = st.w;
    bi.bV5Height      = -st.h;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void* bits = nullptr;
    st.dib = ::CreateDIBSection(st.memDC, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                &bits, nullptr, 0);
    if (!st.dib || !bits) return false;

    st.px = (unsigned char*)bits;
    st.oldBmp = ::SelectObject(st.memDC, st.dib);
    ::memset(st.px, 0, (size_t)st.w * (size_t)st.h * 4);
    return true;
}

// 按目标显示器铺四块边条。
//
// 四角的"羽化"是这么做的：四条边条都【贯通整条边】—— 左右两条贯穿整屏高度、
// 上下两条贯穿整屏宽度，于是每个角落的 T×T 方块由相邻两条边条【重叠】渲染。
// 分层窗口的 over 合成天然近似"取并集"：1-(1-a)(1-b) ≥ max(a,b)，
// 水平辉光与垂直辉光在角落互相补齐 —— 正是一根弯过角落的霓虹管该有的样子。
// 旧版让左右两条"让开四角"，结果是垂直光带在 y=top+T 处以满强度突然出现
// （水平辉光在那里已衰减到 ~3%），沿边缘出现 3%→100% 的生硬断口 —— 已废弃。
//
// 弧长参数按真实周长精确衔接（像素步进数 L = 2(W-1)+2(H-1)）：
//   上 0..W-1 → 右 W-1..W+H-2 → 下 W+H-2..2W+H-3 → 左 2W+H-3..2W+2H-4
// 四个角点处两条边的 s 完全相等，hue 与亮斑相位过角零跳变。
static void Layout(HMONITOR mon) {
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(mon, &mi)) return;

    const RECT r = mi.rcMonitor;
    const int  W = r.right - r.left;
    const int  H = r.bottom - r.top;

    // 光带厚度按屏幕短边取比例，保证不同尺寸屏幕上观感一致
    int T = (int)((W < H ? W : H) * Config::kMarqueeThickRatio + 0.5);
    if (T < Config::kMarqueeThickMin) T = Config::kMarqueeThickMin;
    if (T > Config::kMarqueeThickMax) T = Config::kMarqueeThickMax;
    const int lim = ((W < H ? W : H) / 4);
    if (T > lim) T = lim;
    if (T < 4)   T = 4;

    if (W < 2 * T + 8 || H < 2 * T + 8) return;   // 屏太小，角落会吞掉整条边，算了

    BuildTables(T);

    g_perimeter = 2.0 * (W + H) - 4.0;            // 色相/亮斑归一化的真实周长

    // 上边：x = 0..W-1 → s = 0..W-1（右上角 s=W-1 与右边条起点精确相等）
    Strip& top = g_strip[0];
    top.x = r.left; top.y = r.top; top.w = W; top.h = T;
    top.alongIsX = true; top.perpFlip = false;
    top.thick = T; top.alongLen = W;
    top.sAt0 = 0.0; top.sStep = 1.0;

    // 右边：贯通整屏高度。y = 0..H-1 → s = W-1..W+H-2
    Strip& right = g_strip[1];
    right.x = r.right - T; right.y = r.top;
    right.w = T; right.h = H;
    right.alongIsX = false; right.perpFlip = true;
    right.thick = T; right.alongLen = H;
    right.sAt0 = (double)(W - 1); right.sStep = 1.0;

    // 下边：x = 0..W-1 → s = 2W+H-3 .. W+H-2（右下角与左边条起点精确相等）
    Strip& bottom = g_strip[2];
    bottom.x = r.left; bottom.y = r.bottom - T; bottom.w = W; bottom.h = T;
    bottom.alongIsX = true; bottom.perpFlip = true;
    bottom.thick = T; bottom.alongLen = W;
    bottom.sAt0 = (double)(2 * W + H - 3); bottom.sStep = -1.0;

    // 左边：贯通整屏高度。y = 0..H-1 → s = 2W+2H-4 .. 2W+H-3
    // （顶端 s=2W+2H-4=L 回绕到上边起点的 s=0，闭环成立）
    Strip& left = g_strip[3];
    left.x = r.left; left.y = r.top;
    left.w = T; left.h = H;
    left.alongIsX = false; left.perpFlip = false;
    left.thick = T; left.alongLen = H;
    left.sAt0 = (double)(2 * W + 2 * H - 4); left.sStep = -1.0;

    g_built = true;
    for (int i = 0; i < kStrips; ++i) CreateStrip(g_strip[i]);
}

static void Destroy() {
    for (int i = 0; i < kStrips; ++i) ReleaseStrip(g_strip[i]);
    g_built = false;
    g_alpha = 0.0f;
    g_on    = false;
}

static void HideNow() {
    g_on    = false;
    g_alpha = 0.0f;
    for (int i = 0; i < kStrips; ++i)
        if (g_strip[i].hwnd) ::ShowWindow(g_strip[i].hwnd, SW_HIDE);
}

// 沿边逐列：算出该列的颜色（色相环绕 + 流动亮斑）→ 按垂直剖面刷这一列 → 交给合成器
//
// 关键点：颜色只沿【沿边方向】变化，垂直方向只走衰减 —— 所以每列只算一次颜色，
// 然后一路乘 alpha 写下去，省掉了每像素的 HSV 换算。
static void RenderStrip(Strip& st) {
    if (!st.px || !st.memDC || !st.hwnd) return;
    ::memset(st.px, 0, (size_t)st.w * (size_t)st.h * 4);

    const int    T       = st.thick;
    const int    stride  = st.w;                // 每行 DWORD 数
    const double invP    = (g_perimeter > 0.0) ? (1.0 / g_perimeter) : 0.0;
    const double hueSpan = Config::kMarqueeHueSpan;
    const double waveNum = Config::kMarqueeWaveNum;

    double s = st.sAt0;

    for (int i = 0; i < st.alongLen; ++i, s += st.sStep) {
        // ---- 色相：沿边铺满一圈 + 随时间整体流动（这就是"环绕流动的 RGB"） ----
        double hue = Config::kMarqueeHueBase + g_phase + s * invP * hueSpan;
        while (hue >= 1.0) hue -= 1.0;
        while (hue < 0.0)  hue += 1.0;
        const unsigned char* c = &g_hueLut[((int)(hue * 1024.0) & 1023) * 3];
        const int cr = c[0], cg = c[1], cb = c[2];

        // ---- 沿边流动的亮斑：让整圈不是死板的等亮彩虹 ----
        const double w = (s * invP) * waveNum + g_wavePhase;
        const double e = 1.0 - Config::kMarqueeWaveAmp *
                               (0.5 - 0.5 * std::sin(w * 2.0 * kPi));
        const int A = (int)(e * g_alpha * 255.0 + 0.5);
        if (A <= 0) continue;

        // ---- 按垂直剖面刷这一列 ----
        // p = 从屏幕边缘向内的第几个像素；bufIdx = 它在缓冲区里的行/列号。
        // 上下边（alongIsX）：i 是列号，p 沿行方向 → 步进 stride*4
        // 左右边（!alongIsX）：i 是行号，p 沿列方向 → 步进 4
        unsigned char* col  = st.alongIsX ? (st.px + (size_t)i * 4)
                                          : (st.px + (size_t)i * stride * 4);
        const size_t   step = st.alongIsX ? ((size_t)stride * 4) : 4;

        for (int p = 0; p < T; ++p) {
            const int a = (A * g_prof[p] + 128) >> 8;
            if (a < 2) break;                    // g_prof 随 p 单调递减，后面全透明，收工

            const int bufIdx = st.perpFlip ? (T - 1 - p) : p;

            // 核心几行掺白 → 亮心（"灯丝"感）
            const int wm = g_whiteMix[p];
            int r = cr, g = cg, b = cb;
            if (wm) {
                r += ((255 - r) * wm) >> 8;
                g += ((255 - g) * wm) >> 8;
                b += ((255 - b) * wm) >> 8;
            }

            unsigned char* d = col + (size_t)bufIdx * step;
            d[0] = (unsigned char)((b * a + 128) >> 8);   // 预乘 alpha（ULW_ALPHA 要求）
            d[1] = (unsigned char)((g * a + 128) >> 8);
            d[2] = (unsigned char)((r * a + 128) >> 8);
            d[3] = (unsigned char)a;
        }
    }

    POINT src = { 0, 0 };
    POINT dst = { st.x, st.y };
    SIZE  sz  = { st.w, st.h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::UpdateLayeredWindow(st.hwnd, nullptr, &dst, &sz, st.memDC, &src, 0, &bf, ULW_ALPHA);
}

// ---- 对外接口 ----
static bool Enabled()    { return g_userOn && g_monCount >= 2; }
static bool Built()      { return g_built; }
static bool Busy()       { return g_built && (g_on || g_alpha > 0.004f); }
static int  MonitorCount(){ return g_monCount; }
static bool UserOn()     { return g_userOn; }
static double FrameSec() {
    static_assert(Config::kMarqueeFps > 0, "kMarqueeFps 必须为正，否则 1.0/x 除零");
    return 1.0 / (double)Config::kMarqueeFps;
}

// 托盘菜单切换总开关；关掉时立刻收起
static void SetUserOn(bool on) {
    g_userOn = on;
    if (!on) HideNow();          // HideNow 内部会把 g_on/g_alpha 一起清掉
}

// 显示（不重置相位）；已经在显示则什么都不做
static void Resume() {
    if (!g_built || g_on) return;
    g_on     = true;
    g_lastAt = QpcNow();
    for (int i = 0; i < kStrips; ++i)
        if (g_strip[i].hwnd) ::ShowWindow(g_strip[i].hwnd, SW_SHOWNOACTIVATE);
}

// 从头开始：布局到指定显示器、相位归零、淡入
static void Start(HMONITOR mon) {
    if (!Enabled() || !EnsureClass()) return;

    if (g_built && mon != g_builtFor) Destroy();       // 换屏了 → 重新布局
    if (!g_built) { Layout(mon); g_builtFor = mon; }
    if (!g_built) return;

    g_phase     = 0.0;                                 // 色相从头开始流
    g_wavePhase = 0.0;
    g_alpha     = 0.0f;
    g_on        = false;                               // 交给 Resume() 显示并刷新时间戳
    Resume();
}

static void End() { g_on = false; }

// 出一帧（由主循环按 kMarqueeFps 调度）
static void Frame() {
    const double now = QpcNow();
    double dt = now - g_lastAt;
    g_lastAt = now;
    if (dt < 0.0)  dt = 0.0;
    if (dt > 0.10) dt = 0.10;                          // 卡顿后别让相位跳太多

    const double target = g_on ? 1.0 : 0.0;
    const double step   = dt / Config::kMarqueeFadeSec;
    if (g_alpha < target) g_alpha = (float)((g_alpha + step < target) ? g_alpha + step : target);
    else                  g_alpha = (float)((g_alpha - step > target) ? g_alpha - step : target);

    g_phase += dt / Config::kMarqueeFlowSec;
    while (g_phase >= 1.0) g_phase -= 1.0;

    g_wavePhase += dt / Config::kMarqueeWaveSec;
    while (g_wavePhase >= 1.0) g_wavePhase -= 1.0;

    bool any = false;
    for (int i = 0; i < kStrips; ++i) {
        Strip& st = g_strip[i];
        if (!st.hwnd) continue;
        RenderStrip(st);
        any = true;
    }

    // 淡出结束就收起来（之后 Busy() 为假，主循环不再为它唤醒）
    if (!g_on && g_alpha <= 0.004f && any) {
        g_alpha = 0.0f;
        for (int i = 0; i < kStrips; ++i)
            if (g_strip[i].hwnd) ::ShowWindow(g_strip[i].hwnd, SW_HIDE);
    }
}

// 显示器数量变化（启动、WM_DISPLAYCHANGE）时调用
static void RefreshMonitorCount() {
    int n = ::GetSystemMetrics(SM_CMONITORS);
    if (n < 1) n = 1;
    if (n == g_monCount) return;
    g_monCount = n;
    Destroy();          // 数量变了：先收掉，等下次触发时按新布局重建
}

}  // namespace Marquee

// 触发放大（已在放大态时只刷新计时，不重启动画）
static void OnTrigger(ULONGLONG now) {
    g_lastTrigger = now;
    g_lastActive  = now;

    if (g_target < Config::kScaleMax - 0.001f) {
        g_enlarged = true;
        g_target   = Config::kScaleMax;
        StartAnim(g_scale, Config::kScaleMax, Config::kGrowMs / 1000.0);
        // 多显示器时，在光标所在那块屏的四边拉起跑马灯
        Marquee::Start(g_mon);
    } else {
        g_enlarged = true;   // 已经在最大尺度，仅续命
    }
}

// ===========================================================================
// 主判定循环（TIMER_POLL 驱动）
// ===========================================================================
static void Tick() {
    if (g_shutdown) return;

    POINT pt;
    if (!::GetCursorPos(&pt)) return;

    const ULONGLONG now = ::GetTickCount64();   // 64 位，无 49 天回绕问题
    const bool moved = (pt.x != g_lastPt.x || pt.y != g_lastPt.y);
    bool monChanged = false;
    if (moved) {
        g_lastPt = pt;
        monChanged = EnsureRefreshRate(pt);   // 换显示器时重取刷新率（同屏则秒返回）
    }
    RecordMove(pt, now);

    if (!g_enabled) return;

    // 指针跑到另一块屏了 → 跑马灯跟着搬过去
    if (monChanged && Marquee::Built() && g_enlarged) Marquee::Start(g_mon);

    // ---- 静止快速通道 ----
    // 位置没变 → 速度为 0 → 任何触发条件都不可能成立，整套运动学统计全部跳过。
    // 空闲时这里只花一次坐标比较，这是"无感运行"的关键。
    bool moving = false;
    if (moved) {
        const Motion recent = WindowStats(Config::kCalmWindowSec);
        moving = recent.valid && recent.speed >= Config::kCalmSpeed;
    }
    if (moving) g_lastActive = now;

    if (!g_enlarged) {
        // 指针已经完全缩回原样 → 跑马灯同步淡出
        // （放在这里而不是"开始回缩"时：这样跑马灯与放大状态同生共死，
        //   可见时长从 ~0.5s 提到 ~0.8s，来得及看清；End() 幂等）
        if (Marquee::Busy()) Marquee::End();

        if (!moved) return;             // 没动就不可能触发，直接走
        // 命中任一【已启用】的通道即放大。默认只启用晃动通道（对齐 macOS）。
        // kEnableSwipe 是留给用户的编译期开关：当前默认 false 时下面的与短路
        // 恒为假 —— 这是有意的配置形态，故局部关闭该静态分析警告。
#pragma warning(push)
#pragma warning(disable : 6237)         // C6237: (false && expr)，见上
        if ((Config::kEnableSwipe && DetectSwipe()) || DetectShake())
            OnTrigger(now);
#pragma warning(pop)
        return;
    }

    if (moving) {
        // 运动中：若正处在回缩过程中，则立刻反向放大回去
        if (g_target < Config::kScaleMax - 0.001f) {
            g_target = Config::kScaleMax;
            StartAnim(g_scale, Config::kScaleMax, Config::kGrowMs / 1000.0);
            Marquee::Resume();          // 跑马灯也收回淡出状态
        }
    } else if (g_target > Config::kScaleMin + 0.001f &&
               (now - g_lastActive)  >= (DWORD)Config::kCalmDelayMs &&
               (now - g_lastTrigger) >= (DWORD)Config::kMinHoldMs) {
        // 已平缓足够久：开始平滑回缩（跑马灯不在这里收，等缩回到位再说）
        g_target = Config::kScaleMin;
        StartAnim(g_scale, Config::kScaleMin, Config::kShrinkMs / 1000.0);
    }
}

// 立即恢复到系统光标（禁用 / 退出时用）
static void ForceRestore() {
    StopAnim();
    Marquee::HideNow();
    g_scale    = Config::kScaleMin;
    g_target   = Config::kScaleMin;
    g_enlarged = false;
    g_history.clear();
    RestoreSystemCursor();
}

// ===========================================================================
// 开机自启（注册表 HKCU Run 键）
// ===========================================================================
static std::wstring GetExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return (n > 0) ? std::wstring(buf, n) : std::wstring();
}

static bool IsAutoStartSet() {
    HKEY key;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    LONG r = ::RegQueryValueExW(key, kAppName, nullptr, nullptr, nullptr, nullptr);
    ::RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

static void SetAutoStart(bool on) {
    HKEY key;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                          KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    if (on) {
        std::wstring path = GetExePath();
        ::RegSetValueExW(key, kAppName, 0, REG_SZ,
                         (BYTE*)path.c_str(),
                         (DWORD)((path.size() + 1) * sizeof(wchar_t)));
    } else {
        ::RegDeleteValueW(key, kAppName);
    }
    ::RegCloseKey(key);
}

// ===========================================================================
// 托盘菜单
// ===========================================================================
static void ShowTrayMenu(POINT pt) {
    const int monCount = Marquee::MonitorCount();

    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, 1, L"启用摇晃放大");
    ::CheckMenuItem(menu, 1, MF_BYCOMMAND | (g_enabled ? MF_CHECKED : MF_UNCHECKED));

    // 只有一块屏时这一项没有意义，置灰并说明原因
    if (monCount >= 2) {
        ::AppendMenuW(menu, MF_STRING, 4, L"多屏跑马灯");
        ::CheckMenuItem(menu, 4, MF_BYCOMMAND | (Marquee::UserOn() ? MF_CHECKED : MF_UNCHECKED));
    } else {
        ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 4, L"多屏跑马灯（仅一个显示器）");
    }

    ::AppendMenuW(menu, MF_STRING, 2, L"开机自启动");
    ::CheckMenuItem(menu, 2, MF_BYCOMMAND | (IsAutoStartSet() ? MF_CHECKED : MF_UNCHECKED));
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, 3, L"退出");

    ::SetForegroundWindow(g_hwnd);
    int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                               pt.x, pt.y, 0, g_hwnd, nullptr);
    ::DestroyMenu(menu);

    switch (cmd) {
        case 1:
            g_enabled = !g_enabled;
            if (!g_enabled) ForceRestore();
            break;
        case 2:
            SetAutoStart(!IsAutoStartSet());
            break;
        case 3:
            ::PostMessageW(g_hwnd, WM_COMMAND, 3, 0);
            break;
        case 4:
            Marquee::SetUserOn(!Marquee::UserOn());
            break;
        default:
            break;
    }
}

// ===========================================================================
// 主窗口过程
// ===========================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            // 采样与动画帧统一由主循环的可等待定时器驱动，这里不再用 WM_TIMER
            g_nid.cbSize = sizeof(g_nid);
            g_nid.hWnd   = hwnd;
            g_nid.uID    = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            // 托盘图标优先用内嵌的应用图标（app.ico，资源 ID 1）；
            // 资源缺失时退回系统箭头图形，再退回通用应用图标。
            g_nid.hIcon = (HICON)::LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                              ::GetSystemMetrics(SM_CXICON),
                                              ::GetSystemMetrics(SM_CYICON), LR_SHARED);
            if (!g_nid.hIcon)
                g_nid.hIcon = (HICON)::LoadImage(nullptr, MAKEINTRESOURCE(OCR_NORMAL),
                                                 IMAGE_CURSOR, 0, 0, LR_SHARED);
            if (!g_nid.hIcon) g_nid.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
            wcscpy_s(g_nid.szTip, L"ShakeFindCursor — 摇动鼠标放大指针");
            ::Shell_NotifyIconW(NIM_ADD, &g_nid);
            g_trayAdded = true;
            return 0;

        case WM_TRAYICON:
            if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) {
                POINT pt;
                ::GetCursorPos(&pt);
                ShowTrayMenu(pt);
            }
            return 0;

        case WM_DISPLAYCHANGE:        // 分辨率 / 刷新率 / 显示器数量变了
            g_mon = nullptr;
            Marquee::RefreshMonitorCount();
            return 0;

        case WM_COMMAND:
            if (LOWORD(wp) == 3) {
                g_shutdown = true;
                ForceRestore();
                ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_QUERYENDSESSION:      // 注销 / 关机询问
            ForceRestore();
            return TRUE;

        case WM_ENDSESSION:           // 注销 / 关机确认
            if (wp) ForceRestore();
            return 0;

        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_trayAdded) ::Shell_NotifyIconW(NIM_DELETE, &g_nid);
            StopAnim();
            Marquee::Destroy();
            RestoreSystemCursor();
            ::PostQuitMessage(0);
            return 0;

        default:
            return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ===========================================================================
// 还原兜底：看门狗子进程 / 崩溃过滤器 / 命令行 --restore
// ===========================================================================
// 看门狗：只等父进程结束，然后无条件还原光标。
// 它覆盖"父进程被 TerminateProcess 强杀"这种优雅退出代码跑不到的情况。
static int RunWatchdog(unsigned long parentPid) {
    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)parentPid);
    if (!h) return 2;
    ::WaitForSingleObject(h, INFINITE);
    ::CloseHandle(h);
    ::SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    return 0;
}

// 主进程启动时把自己再拉起来一份当看门狗
static void StartWatchdog() {
    if (!g_hwnd) return;

    wchar_t exe[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;

    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --watchdog " +
                       std::to_wstring((unsigned long)::GetCurrentProcessId());

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (::CreateProcessW(exe, &cmd[0], nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
}

// 崩溃时先还原光标，再把异常交回系统默认处理（保留 WER 上报）
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS*) {
    ::SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    return EXCEPTION_CONTINUE_SEARCH;
}

// 在独立线程里拉看门狗。
// 关键：CreateProcess 在某些受限环境（沙箱、安全软件拦截）下可能长时间阻塞，
// 若放在主线程会直接卡死消息循环 → 定时器全部失效 → 整个程序"看着在跑但完全没反应"。
static DWORD WINAPI WatchdogThread(LPVOID) {
    StartWatchdog();
    return 0;
}

static void SpawnWatchdogAsync() {
    HANDLE th = ::CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr);
    if (th) ::CloseHandle(th);
}

// ===========================================================================
// 入口
// ===========================================================================
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR pCmdLine, _In_ int) {
    // ---- 命令行分支 ----
    if (pCmdLine && *pCmdLine) {
        if (wcsstr(pCmdLine, L"--watchdog")) {
            const wchar_t* p = wcsstr(pCmdLine, L"--watchdog") + 10;
            while (*p == L' ' || *p == L'\t') ++p;
            return RunWatchdog(wcstoul(p, nullptr, 10));
        }
        if (wcsstr(pCmdLine, L"--restore")) {
            // 一键还原：清掉任何残留的放大光标，然后立刻退出
            ::SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
            return 0;
        }
    }

    g_hInst = hInstance;

    // 声明为 per-monitor DPI aware：多屏、不同缩放时保证我们的坐标与物理像素 1:1，
    // 否则跑马灯图层会被 DWM 拉伸而发糊。旧系统上退化为"系统级 DPI 感知"。
    // 动态取函数指针，避免导入表在旧 Windows 上直接加载失败。
    {
        typedef BOOL (WINAPI *PFN_SetDpiCtx)(HANDLE);
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        PFN_SetDpiCtx fn = user32
            ? (PFN_SetDpiCtx)::GetProcAddress(user32, "SetProcessDpiAwarenessContext")
            : nullptr;
        if (fn) fn((HANDLE)(INT_PTR)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        else    ::SetProcessDPIAware();
    }

    // 启动先清一次：万一上次是被强杀的、系统光标残留放大，这里顺手治好
    ::SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);

    ::SetUnhandledExceptionFilter(CrashFilter);

    // QPC 时间基准（动画的帧时刻全靠它，不依赖会被系统节拍量化的 tick）
    ::QueryPerformanceFrequency(&g_qpf);
    if (g_qpf.QuadPart == 0) g_qpf.QuadPart = 1;

    // 常驻内存 DC，给 MakePointerCursor 建 DIB 用（省掉每帧 GetDC/ReleaseDC）
    g_memDC = ::CreateCompatibleDC(nullptr);

    // 高精度可等待定时器：亚毫秒精度，且 **不需要** timeBeginPeriod。
    // timeBeginPeriod 会把整个系统的时钟分辨率抬到 1ms，阻止 CPU 进入深度睡眠，
    // 是持续耗电的大头 —— 这个 API 就是为了取代它而生的（Win10 1803+）。
    g_animTimer = ::CreateWaitableTimerExW(nullptr, nullptr,
                     CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!g_animTimer) {
        // 老系统兜底：普通可等待定时器（精度受系统节拍限制，但功能正常）
        g_animTimer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }

    // 开机就把当前显示器刷新率取回来，免得第一帧用错帧长
    {
        POINT p0;
        if (::GetCursorPos(&p0)) EnsureRefreshRate(p0);
    }

    // 记录显示器数量：>=2 才启用跑马灯
    Marquee::RefreshMonitorCount();

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gsi, nullptr) != Gdiplus::Ok) {
        if (g_animTimer) { ::CloseHandle(g_animTimer); g_animTimer = nullptr; }
        if (g_memDC)     { ::DeleteDC(g_memDC); g_memDC = nullptr; }
        return 1;
    }

    // 按当前系统光标实际尺寸校准原生边长
    int cx = ::GetSystemMetrics(SM_CXCURSOR);
    if (cx >= 16 && cx <= 256) g_nativePx = cx;
    else                       g_nativePx = Config::kNativePx;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(1),
                                           IMAGE_ICON, 0, 0, LR_SHARED);
    wc.hIconSm       = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                           ::GetSystemMetrics(SM_CXSMICON),
                                           ::GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.lpszClassName = L"ShakeFindCursorWnd";
    if (!::RegisterClassExW(&wc)) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        if (g_animTimer) ::CloseHandle(g_animTimer);
        if (g_memDC)     ::DeleteDC(g_memDC);
        return 1;
    }

    g_hwnd = ::CreateWindowExW(0, wc.lpszClassName, kAppName,
                               WS_OVERLAPPED, 0, 0, 0, 0,
                               nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        if (g_animTimer) ::CloseHandle(g_animTimer);
        if (g_memDC)     ::DeleteDC(g_memDC);
        return 1;
    }
    ::ShowWindow(g_hwnd, SW_HIDE);

    // 窗口就绪后再拉看门狗（异步，绝不阻塞消息循环）
    SpawnWatchdogAsync();

    // ---- 主循环 ----
    // 只有一个高精度可等待定时器，同时驱动两件事：
    //   · 采样轮询（每 kPollMs 一次 Tick）
    //   · 动画出帧（动画期间每 1/刷新率 一帧）
    // 每次阻塞前算出「最近的那个到期时刻」装填定时器，醒来先把到期的活儿干完，
    // 再排空消息队列。空闲时按 kPollMs 唤醒（与系统时钟节拍基本重合，不额外耗电），
    // 并且**全程不需要 timeBeginPeriod**。
    //
    // ⚠️ 必须每次都把消息排空：早先的写法在动画帧分支里 `continue` 跳过了排空，
    //    消息会在队列里积压，之后 MsgWaitForMultipleObjectsEx 便不再为"已经在队列里"
    //    的输入返回 → 整个程序假死（轮询停摆、指针卡在放大状态回不来）。
    const double pollSec    = Config::kPollMs / 1000.0;
    const double marqueeSec = Marquee::FrameSec();
    double nextPollAt    = QpcNow() + pollSec;
    double nextMarqueeAt = 0.0;          // 0 = 跑马灯未排期

    bool done = false;
    while (!done) {
        // 1) 装填定时器到「最近到期时刻」（已过期就夹一个小正数，避免空转）
        {
            const double now = QpcNow();
            double wakeAt = nextPollAt;
            if (g_animActive && g_nextFrameAt < wakeAt) wakeAt = g_nextFrameAt;
            if (Marquee::Busy() && nextMarqueeAt > 0.0 && nextMarqueeAt < wakeAt)
                wakeAt = nextMarqueeAt;

            double wait = wakeAt - now;
            if (wait < 0.0002) wait = 0.0002;

            if (g_animTimer) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)(wait * 1e7 + 1.0);  // 负数 = 相对时间(100ns)
                ::SetWaitableTimer(g_animTimer, &due, 0, nullptr, nullptr, FALSE);
                ::MsgWaitForMultipleObjectsEx(1, &g_animTimer, INFINITE, QS_ALLINPUT,
                                              MWMO_INPUTAVAILABLE);
            } else {
                // 极端兜底：连普通可等待定时器都建不出来时，用带超时的等待
                ::MsgWaitForMultipleObjectsEx(0, nullptr, (DWORD)(wait * 1000.0) + 1,
                                              QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }

        // 2) 干到期的活儿
        {
            const double t = QpcNow();
            if (g_animActive && t >= g_nextFrameAt) AnimStep();
            if (t >= nextPollAt) {
                Tick();
                nextPollAt += pollSec;
                if (nextPollAt <= t) nextPollAt = t + pollSec;   // 落后太多就对齐到现在
            }

            // 跑马灯独立按 kMarqueeFps 出帧（慢速光晕，不需要跟刷新率）
            if (Marquee::Busy()) {
                if (nextMarqueeAt <= 0.0 || t >= nextMarqueeAt) {
                    Marquee::Frame();
                    nextMarqueeAt = (nextMarqueeAt <= 0.0) ? (t + marqueeSec)
                                                           : (nextMarqueeAt + marqueeSec);
                    if (nextMarqueeAt <= t) nextMarqueeAt = t + marqueeSec;
                }
            } else {
                nextMarqueeAt = 0.0;
            }
        }

        // 3) 排空消息队列（托盘、会话结束、WM_QUIT 等）
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { done = true; break; }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    StopAnim();
    Marquee::Destroy();
    RestoreSystemCursor();
    FreeCursorBits();
    if (g_animTimer) { ::CloseHandle(g_animTimer); g_animTimer = nullptr; }
    if (g_memDC)     { ::DeleteDC(g_memDC);        g_memDC     = nullptr; }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}
