// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

#include "ReplayHud.h"

#include <optional>

namespace rareplay::ui
{
struct ReplayHudRegions
{
    UiRect mFooter;
    UiRect mSidebar;
};

/** 将两块原生保留区投影到呈现器客户区。 */
ReplayHudRegions GetReplayHudRegions(const std::optional<viewer::NativeUiLayout>& layout,
    float displayWidth, float displayHeight, UiRect gameContentRect) noexcept;
}
