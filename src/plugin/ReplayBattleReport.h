// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "../game_api/ReplayBattleStats.h"

#include <filesystem>

namespace rareplay::plugin
{
void SaveBattleReport(const std::filesystem::path& replay, std::uint32_t frames,
    std::int64_t recordedMilliseconds, const game_api::BattleStats& stats) noexcept;
}
