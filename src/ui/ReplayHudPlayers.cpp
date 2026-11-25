// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#include "ReplayHudParts.h"

#include <cstdio>

namespace rareplay::ui::detail
{
namespace
{
ImVec4 GetPlayerColor(std::uint32_t rgb)
{
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, 1.0f);
}

void DrawPlayerRow(const viewer::PlayerSummary& player, bool isSelected, int& selectedHouseIndex)
{
    ImGui::PushID(player.mHouseIndex);
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float rowHeight = 83.0f;
    if (ImGui::InvisibleButton("选择玩家", ImVec2(rowWidth, rowHeight)))
    {
        selectedHouseIndex = player.mHouseIndex;
    }
    const ImVec2 rowEnd(rowStart.x + rowWidth, rowStart.y + rowHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 fill = isSelected ? IM_COL32(89, 75, 43, 240) : IM_COL32(36, 43, 37, 215);
    drawList->AddRectFilled(rowStart, rowEnd, fill, 4.0f);
    if (isSelected)
    {
        drawList->AddRect(rowStart, rowEnd, IM_COL32(223, 177, 74, 255), 4.0f);
    }
    drawList->AddRectFilled(ImVec2(rowStart.x + 8.0f, rowStart.y + 9.0f),
                            ImVec2(rowStart.x + 20.0f, rowStart.y + 21.0f),
                            ImGui::ColorConvertFloat4ToU32(GetPlayerColor(player.mColorRgb)), 2.0f);

    const char* playerName = player.mName.empty() ? "未命名玩家" : player.mName.c_str();
    char title[256];
    std::snprintf(title, sizeof(title), "%s%s", playerName, player.mDefeated ? " · 已战败" : "");
    const ImVec4 titleClip(rowStart.x + 26.0f, rowStart.y + 4.0f,
                            rowEnd.x - 5.0f, rowStart.y + 24.0f);
    const ImVec4 bodyClip(rowStart.x + 9.0f, rowStart.y + 24.0f,
                           rowEnd.x - 5.0f, rowEnd.y - 2.0f);
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 26.0f, rowStart.y + 6.0f),
                      IM_COL32(239, 235, 217, 255), title, nullptr, 0.0f, &titleClip);
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 9.0f, rowStart.y + 25.0f),
                      IM_COL32(171, 189, 166, 255),
                      player.mFaction.empty() ? "阵营 —" : player.mFaction.c_str(),
                      nullptr, 0.0f, &bodyClip);

    char economy[128];
    if (player.mCredits)
    {
        std::snprintf(economy, sizeof(economy), "资金 %lld", static_cast<long long>(*player.mCredits));
    }
    else
    {
        std::snprintf(economy, sizeof(economy), "资金 —");
    }
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 9.0f, rowStart.y + 44.0f),
                      IM_COL32(218, 218, 203, 255), economy, nullptr, 0.0f, &bodyClip);

    char power[128];
    if (player.mPowerOutput && player.mPowerDrain)
    {
        std::snprintf(power, sizeof(power), "电力 %d / %d%s", *player.mPowerOutput,
                      *player.mPowerDrain, *player.mPowerOutput < *player.mPowerDrain ? " · 停电" : "");
    }
    else
    {
        char output[24];
        char drain[24];
        if (player.mPowerOutput)
        {
            std::snprintf(output, sizeof(output), "%d", *player.mPowerOutput);
        }
        else
        {
            std::snprintf(output, sizeof(output), "—");
        }
        if (player.mPowerDrain)
        {
            std::snprintf(drain, sizeof(drain), "%d", *player.mPowerDrain);
        }
        else
        {
            std::snprintf(drain, sizeof(drain), "—");
        }
        std::snprintf(power, sizeof(power), "电力 %s / %s", output, drain);
    }
    const bool hasOutage = player.mPowerOutput && player.mPowerDrain && *player.mPowerOutput < *player.mPowerDrain;
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 9.0f, rowStart.y + 63.0f),
                      hasOutage ? IM_COL32(241, 129, 106, 255) : IM_COL32(176, 198, 176, 255),
                      power, nullptr, 0.0f, &bodyClip);

    ImGui::PopID();
}
}

void DrawPlayerList(const viewer::ReplayViewSnapshot& snapshot, int& selectedHouseIndex, float height)
{
    if (ImGui::BeginChild("玩家列表", ImVec2(0.0f, height), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        if (snapshot.mPlayers.empty())
        {
            ImGui::TextDisabled("暂无玩家数据");
        }
        for (const auto& player : snapshot.mPlayers)
        {
            DrawPlayerRow(player, selectedHouseIndex == player.mHouseIndex, selectedHouseIndex);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
    }
    ImGui::EndChild();
}

void DrawPlayerStatistics(const viewer::PlayerSummary* player, std::uint32_t completedFrames)
{
    if (!player || !player->mBattleNumbers)
    {
        ImGui::TextDisabled("暂无对局统计");
        return;
    }

    const auto& numbers = *player->mBattleNumbers;
    const char* outcome = (numbers.mFlags & viewer::Winner) ? "获胜" :
        (numbers.mFlags & (viewer::Defeated | viewer::Loser)) ? "已战败" : "作战中";
    ImGui::TextWrapped("%s · %s", player->mName.c_str(), outcome);
    if (!player->mFaction.empty())
        ImGui::TextWrapped("%s", player->mFaction.c_str());
    ImGui::TextDisabled("进度 %.2f 游戏分钟", completedFrames / 3600.0);
    if (player->mCommands && completedFrames)
    {
        const auto count = *player->mCommands;
        ImGui::TextWrapped("指令 %u · %.1f / 游戏分钟", count, count * 3600.0 / completedFrames);
    }

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame;
    ImGui::Spacing();
    ImGui::TextUnformatted("战斗");
    if (ImGui::BeginTable("##BattleSummary", 4, flags))
    {
        ImGui::TableSetupColumn("项目");
        ImGui::TableSetupColumn("单位");
        ImGui::TableSetupColumn("建筑");
        ImGui::TableSetupColumn("合计");
        ImGui::TableHeadersRow();
        const auto row = [](const char* title, int units, int buildings)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(title);
            for (const int value : {units, buildings, units < 0 || buildings < 0 ? -1 : units + buildings})
            {
                ImGui::TableNextColumn();
                if (value < 0) ImGui::TextDisabled("—");
                else ImGui::Text("%d", value);
            }
        };
        row("击杀", numbers.mUnitKills, numbers.mBuildingKills);
        row("战损", numbers.mUnitLosses, numbers.mBuildingLosses);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("生产与存量");
    if (ImGui::BeginTable("##ProductionSummary", 3, flags))
    {
        ImGui::TableSetupColumn("类别");
        ImGui::TableSetupColumn("生产");
        ImGui::TableSetupColumn("持有");
        ImGui::TableHeadersRow();
        const auto row = [](const char* title, int produced, int owned)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(title);
            ImGui::TableNextColumn();
            ImGui::Text("%d", produced);
            ImGui::TableNextColumn();
            ImGui::Text("%d", owned);
        };
        row("步兵", numbers.mProduced.mInfantry, numbers.mOwned.mInfantry);
        row("载具", numbers.mProduced.mVehicles, numbers.mOwned.mVehicles);
        row("飞机", numbers.mProduced.mAircraft, numbers.mOwned.mAircraft);
        row("建筑", numbers.mProduced.mBuildings, numbers.mOwned.mBuildings);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("经济");
    ImGui::Text("采矿收入 %d", numbers.mHarvested);
    ImGui::Text("累计支出 %d", numbers.mSpent);
    ImGui::Text("剩余资金 %d", numbers.mCash);
    ImGui::TextWrapped("电力产出 / 消耗  %d / %d", numbers.mPowerOutput, numbers.mPowerDrain);
}
}
