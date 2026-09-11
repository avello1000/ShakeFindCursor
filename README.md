# ShakeFindCursor — 摇动鼠标放大指针（仿 macOS）

[![CI](https://github.com/avello1000/ShakeFindCursor/actions/workflows/ci.yml/badge.svg)](https://github.com/avello1000/ShakeFindCursor/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/avello1000/ShakeFindCursor)](https://github.com/avello1000/ShakeFindCursor/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Windows 上**快速来回晃动**鼠标，指针平滑放大到约 3 倍，方便快速定位丢失的指针；手停下来后自动平滑缩回原大小。对应 macOS 的 **"Shake mouse pointer to locate"**。

## English

ShakeFindCursor is a tiny Windows tray utility that mimics macOS **"Shake mouse pointer to locate"**: shake your mouse quickly and the pointer smoothly enlarges (~3×) so you can find it; it shrinks back when you stop. On multi-monitor setups, the screen the pointer is on lights up with a **Siri-style RGB border glow**. Single small binary, no admin rights required. (Documentation is in Chinese; the parameter table below is self-explanatory.)

## 功能

- 🔍 **晃动触发**：快速来回晃几次即放大（对齐 macOS 手感：**单向移动再快也不会触发**）。
- 🎬 **平滑缩放**：放大/回缩都走 easeInOutCubic 缓动（两端速度为 0，起步不窜、收尾不急刹）。
- 🖥️ **动画锁定屏幕刷新率**：帧间隔 = 1/刷新率（实测 165Hz → 6.06ms），落在绝对时间栅格上不累积漂移。
- 🎏 **多屏跑马灯**：接了 **2 个及以上**显示器时，晃动放大期间会在**光标所在那块屏**的四边亮起一圈 **Siri 风格 RGB 环绕流光**（整圈色相渐变、随时间流动、带白色"灯丝"亮心），四角羽化过渡自然。**单显示器时不出现**，只放大指针。
- 🪶 **无感运行**：空闲时 CPU 占用**低于计量精度下限**（实测 5 秒 0.0ms）；不用 `timeBeginPeriod`，不影响系统省电。
- ⏱️ **自动恢复**：速度回落到平缓阈值以下并持续约 0.18s 后开始回缩。
- 🎨 **高清指针**：GDI+ 抗锯齿矢量绘制（白色箭头 + 深灰描边 + 投影），不是位图缩放，清晰不糊。
- 🌐 **系统托盘常驻**：右键菜单「启用摇晃放大 / 多屏跑马灯 / 开机自启动 / 退出」（只有一块屏时跑马灯那项会置灰并注明原因）。
- 🛟 **不会卡住指针**：正常退出、注销/关机、崩溃都有还原兜底；**即使被任务管理器强杀，看门狗进程也会自动还原**。
- 🧯 **一键还原**：双击 `restore-cursor.bat`，或运行 `ShakeFindCursor.exe --restore`。
- ✅ **不需要管理员权限**：实测 `SetSystemCursor` 在普通权限下同样生效，已改为 `asInvoker`（启动不再弹 UAC）。

## 编译

```bat
build.bat
```

产物为 `ShakeFindCursor.exe`（含 `app.rc` 编译的版本信息与应用图标，`rc /c 65001` 支持 UTF-8 中文描述）。脚本依赖本机 MSVC（VS2022 Build Tools），编译失败会明确报错退出。每次推送到 main 都会由 [GitHub Actions](.github/workflows/ci.yml) 在 Windows 环境自动构建并上传产物。

## 使用

```bat
ShakeFindCursor.exe              :: 常驻运行，托盘处右键操作
ShakeFindCursor.exe --restore    :: 一键还原光标（不需要管理员，不弹 UAC）
```

万一指针卡在放大状态，双击 **`restore-cursor.bat`** 即可。

## 为什么不会再把指针卡在放大状态（v4.4）

`SetSystemCursor` 是**全局**替换系统光标，程序不主动还原它就一直留着。实测确认：

- **优雅退出（托盘「退出」）本来就是好的** —— 端到端实测 `32px → 96px → 32px`，还原正确。
- **只有"进程被强杀/崩溃"才会留下残留** —— `WM_DESTROY` 里的还原代码根本没机会执行。

所以做了四层兜底：

| 层 | 覆盖场景 | 实现 |
|---|---|---|
| 1 | 托盘「退出」、`WM_DESTROY` | `RestoreSystemCursor()` |
| 2 | 注销 / 关机 | `WM_QUERYENDSESSION` / `WM_ENDSESSION` |
| 3 | 程序崩溃 | `SetUnhandledExceptionFilter` 里先还原再交给系统 |
| 4 | **被任务管理器强杀 / 崩溃到连过滤器都没跑** | **看门狗子进程** |

**看门狗**：主进程启动时用独立线程 spawn 一份自己（`ShakeFindCursor.exe --watchdog <pid>`），它只做一件事——`WaitForSingleObject` 等父进程结束（无论怎么结束），然后执行 `SPI_SETCURSORS` 还原光标。父进程没了，它也随即退出。

> 端到端实测：晃动到 96px 后立刻 `TerminateProcess`（模拟任务管理器强杀）→ 3 秒后指针自动回到 32px。

另外程序**启动时会先无条件还原一次光标**，所以万一上次是被强杀的留下了残留，重新打开程序也就顺手治好了。

`e2e-test.cpp` 是配套的端到端回归测试器（合成晃动、读 `IDC_ARROW` 槽位图宽度、模拟强杀），改了灵敏度参数后可以用它复验。

## 触发逻辑：为什么只认"晃动"（v4.3）

macOS 的 shake-to-find **只有"来回摆动"这一条触发路径**，它不响应任何单向移动。这是它"不会误触发"的根本原因。本工具采用同样的策略。

**通道 B（默认启用）— 快速晃动**

| 条件 | 阈值 | 作用 |
|---|---|---|
| 有效摆动次数 | ≥ `kShakeReversals`（3） | 在 `kShakeWindowSec`（0.55s）内至少来回摆 3 次 |
| 单次摆幅 | ≥ `kShakeMinAmpPx`（24px） | **从极值点反向回撤够多才算一次摆动**——手部微颤永远够不到 |
| 近期速度 | ≥ `kShakeSpeed`（550 px/s） | "晃"必须够快，慢悠悠地摆不算 |
| 净位移/路程 | ≤ `kShakeMaxNetRatio`（0.45） | 必须是"来回摆"，**朝一个方向拖过去会被这条排除** |

摆动计数用的是**转折点式分析**（`CountOscillations`），而不是数相邻采样的位移符号翻转。x / y 两轴分别统计取最大值，所以左右晃、上下晃、斜向晃、画圈都能识别。

**通道 A（默认关闭）— 快速直线滑动**

`kEnableSwipe = false`。这条通道是"快速甩一下鼠标就放大"，日常用鼠标几乎必然误触发（正常把指针从屏幕这头划到那头约 1250px/s，旧版阈值只有 1150px/s）。macOS 也没有这个机制，因此默认关闭，只保留为可选开关；打开后阈值也调得很苛刻（1800px/s + 净位移 160px + 直线度 0.85）。

**恢复**：短窗（0.22s）速度连续低于 `kCalmSpeed`（150 px/s）达 `kCalmDelayMs`（180ms），且距上次触发已过 `kMinHoldMs`（300ms）→ 开始平滑回缩。回缩途中如果又动起来，会立刻反向放大回去。窗口取 0.22s 是为了避免摆动转折处的低速段被误判成"已经停下"。

## 灵敏度调节

全部参数集中在 `main.cpp` 顶部 `namespace Config`：

| 常量 | 默认 | 含义 |
|---|---|---|
| `kPollMs` | 16 | 采样轮询间隔（ms），与系统时钟节拍基本重合 |
| `kEnableSwipe` | **false** | 是否启用"快速直线滑动"通道（macOS 无此机制） |
| `kSwipeSpeed` / `kSwipeMinPx` / `kSwipeStraight` | 1800 / 160 / 0.85 | 直线通道阈值（仅 `kEnableSwipe=true` 时生效） |
| `kShakeWindowSec` | 0.55 | 摆动计数窗口（秒） |
| `kShakeMinAmpPx` | **24** | 单次摆动的最小回撤幅度（px）——**这一项最能决定"抗误触"程度** |
| `kShakeReversals` | 3 | 所需有效摆动次数 |
| `kShakeSpeed` | 550 | 晃动速度下限（px/s） |
| `kShakeMaxNetRatio` | 0.45 | 净位移/路程 上限（排除长距离拖动） |
| `kCalmSpeed` / `kCalmDelayMs` / `kMinHoldMs` | 150 / 180 / 300 | 恢复判定（px/s、ms、ms） |
| `kScaleMax` | 3.0 | 放大倍数（位图边长 = 原生边长 × 该值） |
| `kGrowMs` / `kShrinkMs` | 180 / 300 | 放大 / 回缩动画时长（ms） |
| `kAnimMinFps` / `kAnimMaxFps` | 24 / 240 | 刷新率读到异常值时的夹取范围 |
| `kMarqueeEnable` | true | 多屏跑马灯总开关（托盘菜单也能切） |
| `kMarqueeThickRatio` / `kMarqueeThickMin` / `kMarqueeThickMax` | 0.030 / 18 / 96 | 光带厚度 = 短边 × 比例，再夹到 [min, max]（px） |
| `kMarqueeCorePx` / `kMarqueeCoreWhite` | 3 / 0.80 | "灯丝"核心线宽度与掺白比例 |
| `kMarqueeGlowDecay` | 0.28 | 辉光衰减：σ = 厚度 × 该值（越小光带越"紧"） |
| `kMarqueeSat` / `kMarqueeVal` | 0.80 / 1.00 | 颜色饱和度 / 明度 |
| `kMarqueeHueBase` / `kMarqueeHueSpan` | 0.52 / 1.00 | 色环起点 / 用掉多少色环（1.0 = 整圈彩虹） |
| `kMarqueeFlowSec` | 2.6 | 色相绕屏流一圈的时间（秒） |
| `kMarqueeWaveNum` / `kMarqueeWaveSec` / `kMarqueeWaveAmp` | 3.0 / 3.1 / 0.26 | 沿边流动亮斑：一圈几个 / 流动周期 / 明暗幅度 |
| `kMarqueeFadeSec` / `kMarqueeFps` | 0.25 / 50 | 淡入淡出时长（秒）/ 帧率（慢速流光，不必跟刷新率） |

**嫌还是太灵敏**：把 `kShakeMinAmpPx` 调到 35~45，或把 `kShakeReversals` 调到 4。
**嫌晃不动**：把 `kShakeMinAmpPx` 降到 15，或把 `kShakeReversals` 降到 2。

## 动画与资源占用（v4.5）

**帧率锁定刷新率**：刷新率按**光标所在显示器**查询（`EnumDisplaySettings`，换屏/改分辨率时 `WM_DISPLAYCHANGE` 自动重取），帧间隔 = 1/刷新率。帧时刻落在 `动画起点 + n×帧间隔` 的**绝对时间栅格**上，所以不累积漂移；某帧画慢了就跳帧，而不是把后续帧一起拖慢。

**为什么不用 `WM_TIMER`**：它的精度受系统时钟节拍（默认 ~15.6ms）限制，做不到 165Hz 的 6.06ms，而且和刷新率毫无关系。实测帧间隔抖动会很明显。

**关键取舍：不调用 `timeBeginPeriod`**。那个 API 会把**整个系统**的时钟分辨率抬到 1ms，阻止 CPU 进入深度睡眠，是持续耗电的大头。改用 **高精度可等待定时器**（`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`，Win10 1803+）——它只唤醒本进程，不动系统全局设置。

**实测数据（本机 165Hz）**：

| 指标 | 数值 |
|---|---|
| 帧间隔目标 / 实测 | 6.0606 ms / 5.55–6.62 ms |
| 轮询频率 | 62.5 Hz（主循环与 `Tick` 一致） |
| **空闲 CPU** | **低于计量精度下限**（6 秒测出 0.0ms；旧版 15.6ms） |
| 每次动画 CPU | 约 60ms（旧版约 97ms） |

**每帧成本拆解与优化**：一次动画约装入 57–58 次光标。原本每次都要 `CreateDIBSection` + GDI+ 抗锯齿重绘箭头 + 扫描全图像素生成掩码，约 0.86ms/次。现在**按边长缓存已渲染的位图**（`HCURSOR` 不能缓存——`SetSystemCursor` 会销毁它；但 `CreateIconIndirect` 会复制位图，所以位图可以复用），绘制开销从 ~52ms 降到 ~9ms。剩下的 `SetSystemCursor`（约 50ms/动画）是改动系统光标的系统调用，属于不可再压的硬底。代价是约 1.2MB 的位图缓存。

## 多屏跑马灯（v4.6 引入，v4.7 重做配色与四角）

**什么时候出现**：`GetSystemMetrics(SM_CMONITORS) >= 2`（即接了两个及以上显示器）**且**托盘菜单里「多屏跑马灯」是勾选状态。触发晃动放大后，在**光标所在那块屏**的四边亮起；指针缩回原样时同步淡出。单显示器时完全不创建。

**画面**：Siri 唤醒那种**整圈 RGB 环绕流光** —— 色相沿四边铺满一整圈彩虹并随时间流动（2.6 秒/圈），叠加 3 个沿边流动的明暗亮斑；每条光带最外几像素掺白做出"灯丝"亮心，向内按 σ = 厚度×0.28 高斯衰减成柔和辉光。

![Siri 风格 RGB 环绕流光（差值图，只有光带本身）](docs/screenshot-marquee.png)

![指针外观：五档尺寸渲染，红叉为热点位置](docs/screenshot-pointer.png)

**怎么实现的**：

- 每条边一个 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` 的**穿透置顶**窗口，用 `UpdateLayeredWindow` 做逐像素 alpha 合成 —— 只有这样才能画出真正平滑的光晕。
- **为什么不整屏开一个窗口**：1920×1080 的 32bpp 位图每帧要合成约 8MB，而四条 58px 光带合计只有约 2MB，差 4 倍。
- **四角羽化 = 贯通 + 重叠取并集**：四条光带都**贯通整条边**（左右两条贯穿整屏高度），每个角落的 T×T 方块由相邻两条边条重叠渲染。分层窗口的 over 合成天然近似"取并集"（`1-(1-a)(1-b) ≥ max(a,b)`），水平辉光与垂直辉光在角落互相补齐 —— 相当于一根弯过角落的霓虹管，沿边缘强度处处连续。早期版本让左右两条"让开四角"，垂直光带在 `y = top+T` 处以满强度突然出现（彼处水平辉光已衰减到 ~3%），形成 3%→100% 的生硬断口 —— 已废弃。
- **弧长参数按真实周长精确衔接**：`上 0..W-1 → 右 W-1..W+H-2 → 下 W+H-2..2W+H-3 → 左 2W+H-3..2W+2H-4`，四个角点处两条边的 s 完全相等，色相与亮斑相位过角零跳变，彩虹是真的"绕"过角落。
- 光带是「沿边颜色 × 垂直衰减」的**可分离**结构：沿边每列只算一次颜色（查 1024 档色相 LUT），垂直方向查衰减/掺白表，每像素一次乘法 + 一次 4 字节拷贝；alpha 为 0 的行提前收工。
- 跑马灯按 **50fps** 跑，**不跟刷新率** —— 它是慢速流光，50fps 已足够，也更省资源。
- 边条用完**只隐藏不销毁**，下次触发放出来直接复用；只有显示器数量变化时才重建。

**四角平滑度的量化验证**：`marquee-test.exe` 会沿辉光区（内缩 24px）的边界路径穿过每个角，报告相邻采样点的最大跳变。本机实测四角为 **8 / 21 / 13 / 21**（阈值 26，越小越平滑）。

> ⚠️ 测量陷阱（踩过两次）：① 扫描路径若贴着最外圈走，掺白的浅色"灯丝"芯线在白色任务栏上逐通道差值天然偏小，会把平滑过渡误报成断口 —— 必须走饱和色辉光区；② 抓屏工具必须声明 per-monitor DPI aware，否则 200% 缩放屏上坐标错位（见下）。

**关于 DPI**：程序声明为 per-monitor DPI aware，保证多屏不同缩放时坐标与物理像素 1:1。**否则跑马灯图层会被 DWM 拉伸而发糊**，位置也会错。



## 实现原理

1. 单线程主循环 + 一个高精度可等待定时器，同时驱动「每 `kPollMs` 采样一次」和「动画期间每 1/刷新率 出一帧」；阻塞前装填到最近到期时刻，醒来先干活再排空消息队列。
2. 采样维护轨迹历史（保留 760ms）。**光标位置没变就直接跳过整套运动学判定**——静止时不可能触发，这是空闲近乎零开销的关键。
3. 通道判定命中 → 进入放大态，按刷新率出帧。
4. 每帧按缓动曲线算出尺度；尺寸没变则跳过不装，变了才取（必要时渲染并缓存）箭头位图 → `CreateIconIndirect` → `SetSystemCursor(OCR_NORMAL)`。
5. 平缓足够久后反向动画回缩，动画收尾时用 `SystemParametersInfo(SPI_SETCURSORS)` 交还系统原生光标。
6. 另有看门狗子进程守着，父进程一旦消失就立即还原光标。

> ⚠️ 关键点 1：MSDN 明确说明 `SetSystemCursor` 成功后会用 `DestroyCursor` **销毁**传入的 `hcur`。因此光标**句柄**每次都必须新建、绝不能缓存——否则第二次传入的就是失效句柄，调用会静默失败（这正是旧版"第一次能放大、之后晃不动"的原因）。能缓存的只有 `ICONINFO` 里的**位图**。
>
> ⚠️ 关键点 2：主循环**每次都必须排空消息队列**。早先的写法在动画帧分支里 `continue` 跳过排空，消息积压后 `MsgWaitForMultipleObjectsEx` 便不再为"已在队列里"的输入返回，整个程序会假死——轮询停摆、指针卡在放大状态回不来。

## 说明与限制

- 只替换 `OCR_NORMAL`（主箭头）；文本光标、手型等场景仍是系统默认大小。
- 逐帧 `SetSystemCursor` 在个别机器上可能看到极轻微的刷新感，属系统光标替换机制的固有限制。
- **拖动滑块之类"本来就来回动"的操作也可能触发**——macOS 同样如此，这是晃动检测的固有代价。
- 退出前会先恢复光标；即使发生意外，也有看门狗 + 启动自愈 + `restore-cursor.bat` 三重保险。
- 运行时会多出一个同名的看门狗进程（约 10MB 内存），这是刻意设计的还原兜底，不是异常。
- 不再需要管理员权限，启动不会弹 UAC。
- 动画期间会驻留约 1.2MB 的位图缓存（换取约 5 倍的绘制提速），空闲时 CPU 占用可忽略。
- 跑马灯边条是 TOPMOST，所以**底部光带会压在任务栏上**（不会盖住任务栏的按钮，只在其上叠一层光）。边条用完只隐藏不销毁，占约 1MB。
- 跑马灯只在**光标所在的那块屏**上出现；放大期间把指针甩到另一块屏，它会跟过去并重新淡入。

仓库只保留源码、文本与文档截图；二进制构建产物不入库（请到 [Releases](https://github.com/avello1000/ShakeFindCursor/releases) 下载，或用 `build.bat` 自行构建）。指针外观与跑马灯效果也可以用仓库内自带的两个测试工具在本地复现（见下节）。

## 两个验证工具

改了参数后不用靠肉眼猜，这两支工具可以直接复验（都需要主程序已在跑）：

**`e2e-test.cpp`** — 端到端回归

```bat
cl /nologo /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE e2e-test.cpp ^
   /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:CONSOLE /OUT:e2e-test.exe

e2e-test.exe width      :: 查当前系统箭头尺寸
e2e-test.exe shake      :: 合成晃动并报告尺寸变化
e2e-test.exe exit       :: 走托盘"退出"路径
e2e-test.exe shakekill  :: 放大中强杀（验证看门狗）
e2e-test.exe marquee    :: 枚举跑马灯边条（确认空闲时没有在偷跑）
```

**`marquee-test.cpp`** — 跑马灯视觉验证（抓屏 → 差值图 → 逐边取样 → **四角平滑度扫描**）

```bat
cl /nologo /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE marquee-test.cpp ^
   /link user32.lib gdi32.lib gdiplus.lib /SUBSYSTEM:CONSOLE /OUT:marquee-test.exe

marquee-test.exe
```

> ⚠️ 这两支工具在测「光标尺寸」和「屏幕坐标」时都要留意 DPI：
> 读**系统箭头槽**（`LoadImage(IDC_ARROW)`）才准，读"当前显示的光标"不可靠；
> 抓屏工具本身必须声明 **per-monitor DPI aware**，否则多屏 + 高缩放时拿到的是错位副本，会得出完全错误的结论（我就因此误判过"右边条没渲染"）。

## 变更日志

见 [CHANGELOG.md](CHANGELOG.md)。

## 贡献

欢迎报 bug、提建议、改代码 —— 见 [CONTRIBUTING.md](CONTRIBUTING.md)（含构建与验证流程）。

## 安全

与系统交互面及报告渠道见 [SECURITY.md](SECURITY.md)。

## 许可证

本项目基于 [MIT License](LICENSE) 开源。
