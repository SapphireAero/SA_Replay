#pragma once

#include "../viewer/ReplayViewModel.h"

#include <array>
#include <cstdint>

namespace rareplay::game_api
{
struct ReplayPlayerIdentity
{
    wchar_t mName[24]{};
    bool mIsHuman = false;
    bool mIsSpectator = false;
    bool mKnown = false;
};

using ReplayPlayerIdentities = std::array<ReplayPlayerIdentity, 64>;

ReplayPlayerIdentities ReadPlayerIdentities() noexcept;

struct BattlePlayerStats
{
    std::int32_t mHouse = -1;
    std::int32_t mKills = 0;
    std::int32_t mLosses = 0;
    std::int32_t mProduced = 0;
    wchar_t mName[24]{};
    wchar_t mFaction[48]{};
    viewer::PlayerBattleNumbers mNumbers;
};

struct BattleStats
{
    std::array<BattlePlayerStats, 64> mPlayers{};
    std::uint32_t mCount = 0;
};

BattleStats ReadBattleStats(const ReplayPlayerIdentities& identities) noexcept;
}
