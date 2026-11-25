// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

namespace rareplay::hooks
{
enum class ReplayGraphicsBackend { Unknown, OpenGL, Direct3D9 };

enum class ReplayOverlayStatus
{
    NotStarted,
    WaitingForRenderer,
    HookedWaitingForContext,
    Ready,
    UnsupportedBackend,
    HookFailed
};

struct ReplayOverlayDiagnostics
{
    ReplayGraphicsBackend mObservedBackend = ReplayGraphicsBackend::Unknown;
    ReplayOverlayStatus mStatus = ReplayOverlayStatus::NotStarted;
    unsigned long long mOpenGlSwapCalls = 0;
    unsigned long long mOpenGlContextFrames = 0;
    unsigned long long mD3D9EndSceneCalls = 0;
    unsigned long long mD3D9PresentCalls = 0;
    unsigned long long mRenderedFrames = 0;
    unsigned long long mRenderFailures = 0;
    bool mOpenGlHooked = false;
    bool mD3D9Hooked = false;
    const char* mFailureReason = "none";
};

/**
 * 在有效回放帧中探测已加载的图形 API，分别接入 OpenGL 和 Direct3D 9。
 * 失败只表示覆盖层暂不可用，不应影响回放流程。
 */
bool EnsureReplayOverlayStarted() noexcept;

/** 已观察到能绘制覆盖层的游戏画面，游戏线程可以开始采集 HUD 数据。 */
bool HasReplayOverlayPresenter() noexcept;

/** 供宿主诊断当前覆盖层状态；不会初始化或改变图形状态。 */
ReplayOverlayStatus GetReplayOverlayStatus() noexcept;
ReplayOverlayDiagnostics GetReplayOverlayDiagnostics() noexcept;
}
