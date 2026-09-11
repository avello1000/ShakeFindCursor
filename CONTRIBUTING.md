# 贡献指南

感谢关注 ShakeFindCursor！欢迎以任何形式参与：报 bug、提建议、改代码、改文档都行。

## 开发环境

- Windows 10/11
- Visual Studio 2022（任意版本：Community / Professional / Enterprise / Build Tools 均可），
  需包含 **"使用 C++ 的桌面开发"** 工作负载
- Python 3（仅当需要重新生成 `app.ico` 时用到，运行 `python tools/make_icon.py`）

## 构建与验证

```bat
build.bat
```

`build.bat` 会自动定位 MSVC（vswhere）、编译资源（图标 + 版本信息）并链接出 `ShakeFindCursor.exe`。

改动后请跑一遍仓库自带的两个验证工具（需主程序已在运行）：

```bat
:: 端到端回归：晃动放大/回缩、强杀后看门狗还原、退出还原
e2e-test.exe shake
e2e-test.exe shakekill
e2e-test.exe exit

:: 跑马灯视觉验证：逐边取样 + 四角平滑度扫描（双屏环境）
marquee-test.exe
```

## 提交 PR

1. Fork 后建分支：`git checkout -b feat/your-feature`
2. 小步提交，提交信息说清「为什么改」而不只是「改了什么」
3. 推送并发起 PR；CI 会自动构建，**请确保 CI 绿灯**
4. 描述里写清：动机、改动点、验证方式

## 代码风格

- 与现有代码保持一致：4 空格缩进、中文注释
- 关键算法（触发判定、动画栅格、跑马灯渲染）必须有「为什么这么写」的注释
- 新的可调参数集中放在 `main.cpp` 顶部 `namespace Config`，并在 README 参数表中补一行
- 涉及 `SetSystemCursor` 的改动请特别注意：**句柄不可缓存**（系统会销毁），缓存只允许位图

## 报告问题

优先使用 issue 模板（Bug 报告 / 功能建议）；安全问题请走 `SECURITY.md` 里的私密渠道。
