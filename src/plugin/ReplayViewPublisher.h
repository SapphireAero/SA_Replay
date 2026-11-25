// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "../viewer/ReplayViewModel.h"
#include "../game_api/ReplayWorldReader.h"

namespace rareplay::plugin
{
/** 游戏线程拥有采样节奏与会话标识；图形线程只接收复制后的值。 */
class ReplayViewPublisher final
{
public:
    void Begin(std::string replayName, int recordedLocalHouseIndex);
    void Publish(std::optional<int> playbackSpeedSetting, bool canChangeSpeed,
        std::uint32_t totalFrames, std::optional<std::uint32_t> seekTarget, int playbackRate,
        std::optional<double> actualRate, bool canPause,
        std::uint32_t completedFrames, const viewer::ReplayCommandCounts& commands,
        const game_api::ReplayPlayerIdentities& identities);
    void End();
    std::uint64_t GetSessionId() const noexcept { return mSessionId; }

private:
    std::uint64_t mSessionId = 0;
    std::string mReplayName;
    int mRecordedLocalHouseIndex = -1;
    std::chrono::steady_clock::time_point mNextSample;
    bool mHasLoggedLayout = false;
    game_api::ReplayWorldReader mWorldReader;
    std::uint32_t mIconLogCount = 0;
};
}
