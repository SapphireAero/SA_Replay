<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Modified by SA_Replay contributors. First published with these changes on 2026-11-24. -->

# SA_Replay

SA_Replay 是心灵终结（Mental Omega 3.3.6）的独立回放工具，可与原版启动器共存。支持自动录制、暂停、时间定位、1× 至 16× 播放和对局战报。

## 录像兼容性

SA_Replay 与 [WLChara/RAReplayPlugin](https://github.com/WLChara/RAReplayPlugin) 的 V2 `.yrrep` 录像保持兼容，可直接打开上游生成的录像。SA_Replay 录制的录像也使用 V2 格式。播放需要与录制时匹配的游戏版本和资源，部分旧录像还需要原始地图。

## 安装与使用

1. 退出游戏和原版启动器。将 `SA_Replay.exe`、`RAReplay.Client.dll`、`RAReplayPlugin.dll` 和 `gamemd.exe.manifest` 放到 `gamemd.exe` 所在目录。覆盖前请备份同名文件。
2. 打开 `SA_Replay.exe`，点击「开启自动录制」，再通过原版启动器开始遭遇战或联机对局。录像保存在游戏目录的 `Replays`。
3. 在 SA_Replay 中选中录像后点击「播放选中录像」，也可以打开外部 `.yrrep` 文件。双击列表中的录像可查看地图、玩家和战报。

运行环境为 Windows 和 .NET Framework 4.8。游戏内回放面板支持 OpenGL 和 Direct3D 9。

## 从源码构建

需要 Visual Studio 2022 C++ 工作负载、Windows SDK、CMake 和 .NET SDK。在仓库根目录执行以下命令。

```powershell
.\scripts\build.ps1 -Configuration Release
dotnet build integrations\csharp\ReplayCenter\ReplayCenter.csproj -c Release
```

## 许可证

SA_Replay 派生自 [RAReplayPlugin](https://github.com/WLChara/RAReplayPlugin)，基于上游提交 [`364980b`](https://github.com/WLChara/RAReplayPlugin/commit/364980be1368ee270bb3f08570a5625e63c67c16)。

Copyright (C) 2026 WLChara

SA_Replay 整体依据 GNU 通用公共许可证第 3 版（`GPL-3.0-only`）发布，全文见 [LICENSE](LICENSE)。第三方组件保留各自的版权声明及许可，见 [third_party/README.md](third_party/README.md)。

此前针对「新增及修改部分」的概括性 MIT 声明已撤回。分发编译产物时，请在下载处提供可免费取得的对应完整源码及构建脚本。
