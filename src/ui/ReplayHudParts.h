// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

#include "ReplayHud.h"

namespace rareplay::ui::detail
{
void DrawFpsCard(const viewer::ReplayViewSnapshot& snapshot, bool paused);
void DrawPlayerList(const viewer::ReplayViewSnapshot& snapshot, int& selectedHouseIndex, float height);
void DrawPlayerStatistics(const viewer::PlayerSummary* player, std::uint32_t completedFrames);
void DrawProductionLines(const viewer::PlayerSummary* player, const ReplayHud::IconResolver& iconResolver);
void DrawUnitInventory(const viewer::PlayerSummary* player, const ReplayHud::IconResolver& iconResolver);
void DrawRecordedGroups(const viewer::ReplayViewSnapshot& snapshot);
void DrawFrameControls(const viewer::ReplayViewSnapshot& snapshot, viewer::ReplayViewExchange& exchange,
                       std::optional<std::uint32_t>& dragFrame);
UiRect DrawIcon(const std::shared_ptr<const viewer::IconImage>& icon,
              const ReplayHud::IconResolver& iconResolver, const ImVec2& size);
}
