<!-- SPDX-License-Identifier: GPL-3.0-only -->

# SA_Replay

SA_Replay 是为 Mental Omega 3.3.6 和上游 RAReplay V2 插件配套的独立 Windows 回放客户端，需要 .NET Framework 4.8。

## 开启录制

1. 退出心灵终结启动器和正在运行的游戏。
2. 将 SA_Replay.exe、RAReplay.Client.dll、RAReplayPlugin.dll 和 gamemd.exe.manifest 放到与 gamemd.exe 相同的目录。已有同名文件时，请先备份。
3. 从该游戏目录打开 SA_Replay.exe，点击「开启自动录制」。
4. 重新打开 MentalOmegaClient.exe，在原版大厅开一局遭遇战或联机对战。战役和读档跳过录制。
5. 打完后在 SA_Replay 中点「刷新」。录像保存在游戏目录的 Replays 文件夹，按本地开局日期、开局和结束时间以及地图英文名命名，例如「11.22：21.52-21.58 - Reconcile.yrrep」。地图没有内置名称时使用大厅显示的名称。同名录像会加序号。录制问题可查看 Replays\SA_Replay.log。

录制开关只切换 Resources\ClientDefinitions.ini 里的 GameLauncherExecutableName。首次开启会在旁边备份该配置。从 ReplayCenter 升级时，可在 SA_Replay 中关闭再开启自动录制，将旧启动项切换为 SA_Replay.exe。要移除文件时，请先关闭自动录制。

## 播放录像

播放中可以拖动 SA_Replay 窗口下方的时间条，松开后定位。支持绘制回放面板的渲染模式也可在游戏内拖动时间条。快进会加速重演到目标，回退会重新加载录像后从开头追到目标。录像越长，定位越靠后，等待可能越久。

在时间条旁选择 1×、2×、4×、8× 或 16×。倍速只影响观看节奏，回退后保留所选倍速。实际速度还取决于游戏模拟和画面绘制能跑多快。

点击时间条旁的「暂停」，再点击「继续」恢复播放。游戏内回放面板也有同样的按钮。暂停时可以拖动时间条，定位到目标帧后继续保持暂停。

退出原版启动器和游戏，在 SA_Replay 的列表单击一份录像后点「播放选中录像」，或点「打开录像文件…」直接播放 V2 的 .yrrep 文件。播放完成后，游戏开局文件会恢复。SA_Replay 生成的录像内含这局的地图快照，跨目录播放仍需要匹配的游戏版本和游戏资源。其他 V2 录像可能还需要对应的原始地图。

双击列表中的录像打开详情。在右侧总览选择玩家，可查看单位与建筑的击杀和战损、分类生产与末帧持有量、采矿收入、支出、资金、电力和指令总数。游戏内回放面板右下的「统计」使用同样的口径，显示截至当前帧的数据。每 3,600 个逻辑帧计为 1 游戏分钟。

玩家栏显示原局大厅 ID，电脑玩家统一显示为「AI」。相邻的「阵营」列显示原局阵营。

新录像在结束时保存战报。已有录像会显示现有统计，点「重演生成战报」或「补齐详细统计」可加速重演并保存详细战报。定位或暂停时，游戏内统计跟随当前帧。移动录像时一并移动同名的 .battle-stats 文件即可保留战报。

这套上游插件使用 OpenGL 或 Direct3D 9 显示游戏内回放面板。使用 GDI 时可播放录像，面板不显示。成品回放安装包使用的 V3 帧流不在本程序的支持范围内。

## 从源码构建

先根据仓库根目录的 README 构建 Win32 Release 插件，再在仓库根目录执行：

    dotnet build integrations\csharp\ReplayCenter\ReplayCenter.csproj -c Release

从 integrations\csharp\ReplayCenter\bin\Release\net48 取 SA_Replay.exe、RAReplay.Client.dll 和 gamemd.exe.manifest，再从 build\v2\Release 取 RAReplayPlugin.dll。

## 许可证

SA_Replay 整体按 GPL-3.0-only 发布。上游来源、版权和第三方许可说明见[仓库 README](../../../README.md#许可证)，许可证全文见 [LICENSE](../../../LICENSE)。
