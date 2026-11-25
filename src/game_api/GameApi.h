// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

#include "../replay/ReplayPorts.h"
#include "PlayerInputEffects.h"
#include "ViewerPlaybackSpeed.h"

#include <array>

namespace rareplay::game_api
{
/** 除注明的初始化入口外，只在游戏线程、已验证目标映像且世界就绪后调用。 */
class GameApi final : public replay::IReplayGame
{
public:
    bool IsWorldReady() const;
    bool ReadSessionInfo(replay::ReplaySessionInfo& outInfo, std::string& error) override;
    bool ReadFrameContext(std::uint32_t& outFrameNumber, std::uint32_t& outChecksum,
        bool& outHasChecksum, std::string& error) override;
    bool CopyDueEvents(std::vector<replay::RecordedEvent>& outEvents, std::string& error) override;
    bool CopyPlayerInputEffects(std::vector<replay::RecordedInputEffect>& outEffects,
        std::string& error) override;
    bool ApplyPlayerInputEffects(const std::vector<replay::RecordedInputEffect>& effects,
        std::string& error) override;
    bool RecordPlayerInputEffect(const replay::RecordedInputEffect& effect,
        std::string& error);
    bool ExecuteReplayFrame(const replay::ReplayFrame& frame, std::string& error) override;
    void DiscardLocalCommands() override;
    void EndReplay() override;
    void BeginFrame();
    void BeforeNativeEventExecution();
    bool TrySetViewerPlaybackSpeed(int setting) noexcept;
    bool TryGetViewerPlaybackSpeed(int& outSetting) const noexcept;
    bool CanControlViewerPlaybackSpeed() const noexcept;
    bool IsNetworkGame() const noexcept;
    void RenderPausedPlayback() const;
    const std::array<std::uint32_t, 64>& GetPlaybackCommandCounts() const noexcept { return mPlaybackCommands; }
    static void InvokeNativeFrameParser();
    static bool InvokeNativePreworldWait();
    /** 仅在已验证目标的 InitRandom 选种后、任一 RNG 初始化前的游戏线程调用；不要求世界就绪。 */
    static void RestoreRandomSeedForInitialization(std::uint32_t seed);

private:
    void ProcessViewerCommands();

    PlayerInputEffects mPlayerInputEffects;
    ViewerPlaybackSpeed mViewerPlaybackSpeed;
    std::string mPlayerInputEffectsError;
    std::uint32_t mActiveFrame = 0;
    std::array<std::uint32_t, 64> mPlaybackCommands{};
    bool mIsFrameActive = false;
    bool mHasNativeChecksum = false;
};
}
