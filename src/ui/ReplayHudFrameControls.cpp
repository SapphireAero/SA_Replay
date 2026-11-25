// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#include "ReplayHudParts.h"

#include "../viewer/ReplayViewExchange.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rareplay::ui::detail
{
void DrawFrameControls(const viewer::ReplayViewSnapshot& snapshot, viewer::ReplayViewExchange& exchange,
                       std::optional<std::uint32_t>& dragFrame)
{
    constexpr const char* speedLabels[] = {"最快", "很快", "快", "正常", "慢", "很慢", "最慢"};
    constexpr const char* rateLabels[] = {"1×", "2×", "4×", "8×", "16×"};
    constexpr int rates[] = {1, 2, 4, 8, 16};
    const bool canSeek = snapshot.mActive && !snapshot.mIsPreview && snapshot.mTotalFrames.value_or(0) > 1;
    const bool canPause = snapshot.mActive && !snapshot.mIsPreview && snapshot.mCanPause;
    const float height = ImGui::GetFrameHeight();
    const auto& style = ImGui::GetStyle();
    const auto buttonWidth = [&style](const char* label)
    {
        return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
    };
    float controlsWidth = canPause ? buttonWidth("继续") : 0.0f;
    if (canSeek)
        for (const auto* label : rateLabels)
            controlsWidth += buttonWidth(label);
    const bool compactRates = canSeek && ImGui::GetContentRegionAvail().x - controlsWidth < 100.0f;
    if (compactRates)
        controlsWidth = buttonWidth("16×") + (canPause ? buttonWidth("继续") : 0.0f);
    if (!canSeek && snapshot.mActive && snapshot.mCanChangePlaybackSpeed)
        controlsWidth += buttonWidth("很快");

    const float timelineWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - controlsWidth);
    const std::uint32_t total = snapshot.mTotalFrames.value_or(0);
    if (total > 0)
    {
        const auto last = total - 1;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 end(start.x + timelineWidth, start.y + height);
        const float firstX = start.x + std::min(5.0f, timelineWidth * 0.5f);
        const float lastX = end.x - std::min(5.0f, timelineWidth * 0.5f);
        const bool interactive = canSeek && lastX > firstX;
        if (interactive)
        {
            ImGui::InvisibleButton("##ReplayTimeline", ImVec2(timelineWidth, height));
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool validMouse = ImGui::IsMousePosValid(&mouse);
            const bool released = dragFrame.has_value() && ImGui::IsItemDeactivated() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            if (validMouse && (ImGui::IsItemActive() || released))
            {
                const double position = std::clamp<double>((mouse.x - firstX) / (lastX - firstX), 0.0, 1.0);
                dragFrame = static_cast<std::uint32_t>(std::llround(position * last));
                if (released)
                    exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::Seek, static_cast<int>(*dragFrame)});
            }
        }
        else
            ImGui::Dummy(ImVec2(timelineWidth, height));

        const auto completed = snapshot.mCompletedFrames == 0 ? 0 : snapshot.mCompletedFrames - 1;
        const auto selected = std::min(dragFrame.value_or(snapshot.mSeekTarget.value_or(completed)), last);
        const auto cursorX = firstX + (lastX - firstX) * (last ? static_cast<float>(selected) / last : 0.0f);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, end, ImGui::GetColorU32(ImGuiCol_FrameBg));
        draw->AddRectFilled(start, ImVec2(cursorX, end.y), IM_COL32(91, 78, 46, 255));
        if (interactive)
            draw->AddLine(ImVec2(cursorX, start.y), ImVec2(cursorX, end.y), IM_COL32(238, 192, 94, 255), 2.0f);
        char text[80];
        std::snprintf(text, sizeof(text), "%s%u / %u 帧", dragFrame || snapshot.mSeekTarget ? "定位 " : "",
                      selected + 1, total);
        const auto labelWidth = ImGui::CalcTextSize(text).x;
        if (labelWidth + 8.0f < timelineWidth)
        {
            const ImVec4 clip(start.x, start.y, end.x, end.y);
            draw->AddText(nullptr, 0.0f, ImVec2(start.x + (timelineWidth - labelWidth) * 0.5f, start.y),
                          ImGui::GetColorU32(ImGuiCol_Text), text, nullptr, 0.0f, &clip);
        }
        if (!interactive || !ImGui::IsItemActive())
            dragFrame.reset();
    }
    else
    {
        dragFrame.reset();
        ImGui::Text("当前帧 %u", snapshot.mFrameNumber);
    }

    if (canSeek)
    {
        int selectedRate = 0;
        for (int i = 0; i < 5; ++i)
            if (rates[i] == snapshot.mPlaybackRate)
                selectedRate = i;
        if (compactRates)
        {
            ImGui::SameLine();
            if (ImGui::Button(rateLabels[selectedRate]))
                exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::SetPlaybackRate,
                                 rates[(selectedRate + 1) % 5]});
        }
        else
        {
            for (int i = 0; i < 5; ++i)
            {
                ImGui::SameLine();
                if (i == selectedRate)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(rateLabels[i]))
                    exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::SetPlaybackRate, rates[i]});
                if (i == selectedRate)
                    ImGui::PopStyleColor();
            }
        }
    }
    else if (snapshot.mActive && snapshot.mCanChangePlaybackSpeed)
    {
        const int speed = std::clamp(snapshot.mPlaybackSpeedSetting, 0, 6);
        ImGui::SameLine();
        if (ImGui::Button(speedLabels[speed]))
            exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::SetPlaybackSpeed, (speed + 1) % 7});
    }
    if (canPause)
    {
        const bool pauseRequested = exchange.ReadPauseState() != viewer::PlaybackPauseState::Running;
        ImGui::SameLine();
        if (ImGui::Button(pauseRequested ? "继续###ReplayPause" : "暂停###ReplayPause"))
            exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::SetPaused, pauseRequested ? 0 : 1});
    }
}
}
