#include "ReplayHud.h"

#include "ReplayHudParts.h"
#include "ReplayHudLayout.h"
#include "../viewer/ReplayViewExchange.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rareplay::ui
{
namespace
{
constexpr float EDGE_GAP = 14.0f;
constexpr float FPS_WIDTH = 258.0f;

void RecordWindowRegion(std::vector<UiRect>& regions)
{
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    regions.push_back({position.x, position.y, position.x + size.x, position.y + size.y});
}

UiRect RoundInside(UiRect rect, ImVec2 origin)
{
    return {std::ceil(origin.x + rect.mLeft), std::ceil(origin.y + rect.mTop),
            std::floor(origin.x + rect.mRight), std::floor(origin.y + rect.mBottom)};
}

void SetNextRegion(const UiRect& rect)
{
    ImGui::SetNextWindowPos(ImVec2(rect.mLeft, rect.mTop));
    ImGui::SetNextWindowSize(ImVec2(rect.mRight - rect.mLeft, rect.mBottom - rect.mTop));
}

const viewer::PlayerSummary* FindPlayer(const viewer::ReplayViewSnapshot& snapshot, int houseIndex)
{
    for (const auto& player : snapshot.mPlayers)
    {
        if (player.mHouseIndex == houseIndex)
        {
            return &player;
        }
    }
    return nullptr;
}

void PushHudStyle()
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.105f, 0.125f, 0.118f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.135f, 0.155f, 0.140f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.205f, 0.225f, 0.194f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.28f, 0.30f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.32f, 0.19f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.44f, 0.39f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.53f, 0.43f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.39f, 0.79f, 0.67f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.86f, 0.69f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.25f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.35f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.43f, 0.38f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.20f, 0.24f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.40f, 0.37f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.36f, 0.32f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.20f, 0.24f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.36f, 0.32f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.80f, 0.65f, 0.31f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
}

void PopHudStyle()
{
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(18);
}
}

void ReplayHud::Draw(const viewer::ReplayViewSnapshot& snapshot, viewer::ReplayViewExchange& exchange)
{
    mInputRegions.clear();
    mVisibleRegions.clear();
    if (mSessionId != snapshot.mSessionId)
    {
        mSessionId = snapshot.mSessionId;
        mSelectedHouseIndex = -1;
        mTimelineDragFrame.reset();
        if (!mSelectPanelPending)
        {
            mSelectedPanel = ReplayPanel::Statistics;
        }
    }

    if (FindPlayer(snapshot, mSelectedHouseIndex) == nullptr)
    {
        mSelectedHouseIndex = snapshot.mRecordedLocalHouseIndex;
        if (FindPlayer(snapshot, mSelectedHouseIndex) == nullptr && !snapshot.mPlayers.empty())
        {
            mSelectedHouseIndex = snapshot.mPlayers.front().mHouseIndex;
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 extent = viewport->WorkSize;
    if (extent.x < 400.0f || extent.y < 300.0f)
    {
        return;
    }

    PushHudStyle();
    const ImGuiWindowFlags fixedFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    const float fpsWidth = std::min(FPS_WIDTH, extent.x - EDGE_GAP * 2.0f);
    ImGui::SetNextWindowPos(ImVec2(origin.x + EDGE_GAP, origin.y + EDGE_GAP));
    ImGui::SetNextWindowSize(ImVec2(fpsWidth, 0.0f));
    if (ImGui::Begin("播放状态##ReplayFps", nullptr,
            fixedFlags | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize))
    {
        RecordWindowRegion(mVisibleRegions);
        detail::DrawFpsCard(snapshot, exchange.ReadPauseState() == viewer::PlaybackPauseState::Paused);
    }
    ImGui::End();

    const auto regions = GetReplayHudRegions(snapshot.mNativeUiLayout, extent.x, extent.y, mGameContentRect);
    const auto sidebar = RoundInside(regions.mSidebar, origin);
    const float panelHeight = sidebar.mBottom - sidebar.mTop;
    if (sidebar.mRight - sidebar.mLeft >= 60.0f && panelHeight >= 100.0f)
    {
        const ImVec2 panelPadding(5.0f, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, panelPadding);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));
        SetNextRegion(sidebar);
        if (ImGui::Begin("回放信息##ReplayPanel", nullptr, fixedFlags))
        {
            mInputRegions.push_back(sidebar);
            if (snapshot.mIsPreview)
            {
                ImGui::TextColored(ImVec4(0.96f, 0.71f, 0.35f, 1.0f), "布局预览 · 示例数据");
            }
            if (!snapshot.mActive)
            {
                ImGui::TextDisabled("当前没有活动回放");
            }
            if (!snapshot.mMapName.empty())
            {
                ImGui::TextWrapped("%s", snapshot.mMapName.c_str());
            }
            ImGui::Separator();
            ImGui::TextUnformatted("玩家与经济");
            const float playerListHeight = std::clamp(panelHeight * 0.28f, 84.0f, 172.0f);
            detail::DrawPlayerList(snapshot, mSelectedHouseIndex, playerListHeight);
            ImGui::Separator();

            const viewer::PlayerSummary* selectedPlayer = FindPlayer(snapshot, mSelectedHouseIndex);
            if (ImGui::BeginTabBar("回放详情##ReplayDetails", ImGuiTabBarFlags_FittingPolicyScroll))
            {
                const ImGuiTabItemFlags statisticsFlags = mSelectPanelPending && mSelectedPanel == ReplayPanel::Statistics ?
                    ImGuiTabItemFlags_SetSelected : 0;
                const ImGuiTabItemFlags productionFlags = mSelectPanelPending && mSelectedPanel == ReplayPanel::Production ?
                    ImGuiTabItemFlags_SetSelected : 0;
                const ImGuiTabItemFlags unitFlags = mSelectPanelPending && mSelectedPanel == ReplayPanel::Units ?
                    ImGuiTabItemFlags_SetSelected : 0;
                const ImGuiTabItemFlags groupFlags = mSelectPanelPending && mSelectedPanel == ReplayPanel::Groups ?
                    ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem("统计", nullptr, statisticsFlags))
                {
                    ImGui::BeginChild("统计内容", ImVec2(0.0f, 0.0f), false);
                    detail::DrawPlayerStatistics(selectedPlayer, snapshot.mCompletedFrames);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("建造", nullptr, productionFlags))
                {
                    ImGui::BeginChild("建造内容", ImVec2(0.0f, 0.0f), false);
                    detail::DrawProductionLines(selectedPlayer, mIconResolver);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("单位", nullptr, unitFlags))
                {
                    ImGui::BeginChild("单位内容", ImVec2(0.0f, 0.0f), false);
                    detail::DrawUnitInventory(selectedPlayer, mIconResolver);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("编队", nullptr, groupFlags))
                {
                    ImGui::BeginChild("编队内容", ImVec2(0.0f, 0.0f), false);
                    detail::DrawRecordedGroups(snapshot);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
                mSelectPanelPending = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    const auto footer = RoundInside(regions.mFooter, origin);
    const float footerHeight = footer.mBottom - footer.mTop;
    if (footer.mRight - footer.mLeft >= 210.0f && footerHeight >= 12.0f)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        SetNextRegion(footer);
        if (ImGui::Begin("播放控制##ReplayFooter", nullptr,
                fixedFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            mInputRegions.push_back(footer);
            ImGui::SetWindowFontScale(std::min(1.0f, (footerHeight - 2.0f) / ImGui::GetFont()->FontSize));
            ImGui::SetCursorPosY(std::floor((footerHeight - ImGui::GetFrameHeight()) * 0.5f));
            detail::DrawFrameControls(snapshot, exchange, mTimelineDragFrame);
        }
        ImGui::End();
        ImGui::PopStyleVar(5);
    }
    else
        mTimelineDragFrame.reset();
    mVisibleRegions.insert(mVisibleRegions.end(), mInputRegions.begin(), mInputRegions.end());
    PopHudStyle();
}

void ReplayHud::Reset()
{
    mSessionId = 0;
    mSelectedHouseIndex = -1;
    mTimelineDragFrame.reset();
    mInputRegions.clear();
    mVisibleRegions.clear();
    mSelectedPanel = ReplayPanel::Statistics;
    mSelectPanelPending = false;
}

void ReplayHud::SetIconResolver(IconResolver resolver)
{
    mIconResolver = std::move(resolver);
}

void ReplayHud::SelectPanel(ReplayPanel panel)
{
    mSelectedPanel = panel;
    mSelectPanelPending = true;
}

const std::vector<UiRect>& ReplayHud::GetInputRegions() const noexcept
{
    return mInputRegions;
}

const std::vector<UiRect>& ReplayHud::GetVisibleRegions() const noexcept
{
    return mVisibleRegions;
}
}
