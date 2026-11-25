// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

#include <cstdint>

#include "../replay/ReplayTypes.h"

namespace rareplay::plugin
{
void OnStartup() noexcept;
bool TryGetPlaybackSeed(std::uint32_t& outSeed) noexcept;
void OnFrameBegin() noexcept;
void OnBeforeSimulation() noexcept;
bool ShouldCapturePlayerInputEffect() noexcept;
void OnPlayerInputEffectCaptured(const replay::RecordedInputEffect& effect) noexcept;
void OnPlayerInputEffectCaptureFailed() noexcept;
bool ShouldBlockPlayerCommands() noexcept;
bool TrySetPlaybackViewingSpeed(int setting) noexcept;
bool TryGetPlaybackViewingSpeed(int& outSetting) noexcept;
bool ShouldHandlePlaybackViewingSpeed() noexcept;
bool IsReplaySeeking() noexcept;
int GetReplayPlaybackRate() noexcept;
void WaitScaledReplayFrame(int networkMillis, int offlineTicks) noexcept;
bool PauseAtReplayFrameBoundary() noexcept;
bool ShouldInterruptReplayFrameWait() noexcept;
bool CanInterruptReplayFrameWait() noexcept;
void FinishReplayFrameWait(int networkMillis, int offlineTicks) noexcept;
void OnBeforeNativeEvents() noexcept;
void OnFrameComplete() noexcept;
bool OnPlaybackParser() noexcept;
bool ShouldBypassPeerProgressWait() noexcept;
void OnSessionEnd() noexcept;
void OnHookFailure() noexcept;
}
