# 安全策略

## 支持版本

只对最新 Release 提供安全修复。

## 报告漏洞

请使用 GitHub 的**私密漏洞报告**功能（本仓库 Security 标签页 → "Report a vulnerability"），
不要在公开 issue 中描述可被利用的细节。

## 范围与背景

方便评估一个报告是否有价值，这里是本工具与系统的全部交互面：

- `SetSystemCursor(OCR_NORMAL)` / `SystemParametersInfo(SPI_SETCURSORS)`：全局替换/还原系统箭头光标
- 读写注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`（仅「开机自启动」开关使用）
- 每个显示器边缘的穿透置顶分层窗口（`WS_EX_TRANSPARENT`，不接收任何输入）
- **没有任何网络行为**，不读取、不上传任何用户数据

## 历史已知问题

- 进程被强杀时系统光标可能残留放大 —— 已由看门狗子进程 + 启动自愈 + `--restore` 三重兜底处理（v4.4）。
