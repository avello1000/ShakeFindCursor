@echo off
rem ===========================================================================
rem  一键还原光标
rem
rem  万一鼠标指针卡在放大状态（例如 ShakeFindCursor 被任务管理器强杀、
rem  或系统异常），双击本文件即可立刻还原成系统原生指针。
rem
rem  原理：调用 ShakeFindCursor.exe 的 --restore 模式，
rem        它会无条件执行 SystemParametersInfo(SPI_SETCURSORS) 把光标交还给系统。
rem  注意：不需要管理员权限。
rem ===========================================================================

"%~dp0ShakeFindCursor.exe" --restore
