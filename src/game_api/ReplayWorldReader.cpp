// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#include "ReplayWorldReader.h"
#include "icons/CameoReader.h"

using byte = unsigned char;

#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <FactoryClass.h>
#include <GameOptionsClass.h>
#include <HouseClass.h>
#include <HouseTypeClass.h>
#include <ScenarioClass.h>
#include <SessionClass.h>
#include <SidebarClass.h>
#include <Surface.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <Unsorted.h>

#include <Windows.h>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace rareplay::game_api
{
namespace
{
std::string Utf8(const wchar_t* text, std::size_t maxLength)
{
    if (!text)
        return {};
    const int length = static_cast<int>(wcsnlen_s(text, maxLength));
    if (length <= 0)
        return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), bytes, nullptr, nullptr);
    return result;
}

std::string TypeName(const AbstractTypeClass* type)
{
    if (!type)
        return "未知类型";
    auto name = Utf8(type->UIName, 256);
    return name.empty() ? std::string(type->ID, strnlen_s(type->ID, sizeof(type->ID))) : name;
}

viewer::UnitCategory Category(AbstractType type)
{
    switch (type)
    {
    case AbstractType::Infantry: return viewer::UnitCategory::Infantry;
    case AbstractType::Unit: return viewer::UnitCategory::Vehicle;
    case AbstractType::Aircraft: return viewer::UnitCategory::Aircraft;
    case AbstractType::Building: return viewer::UnitCategory::Building;
    default: return viewer::UnitCategory::Other;
    }
}

ReplayPlayerIdentity ReadPlayerIdentity(const HouseClass& house) noexcept
{
    ReplayPlayerIdentity identity;
    identity.mKnown = true;
    identity.mIsHuman = house.IsHumanPlayer;
    identity.mIsSpectator = house.IsObserver() ||
        (ScenarioClass::Instance && house.IsInitiallyObserver());
    if (!identity.mIsHuman)
    {
        std::copy_n(L"AI", 2, identity.mName);
        return identity;
    }

    // Link the original session name to the house, rather than using the node's array position.
    const auto& nodes = NodeNameType::Array;
    if (nodes.Count > 0 && nodes.Count <= 64 && nodes.Items)
    {
        for (const auto* node : nodes)
        {
            if (node && node->HouseIndex == house.ArrayIndex && node->SpectatorFlag != 0xFFFFFFFFu && node->Name[0])
            {
                std::copy_n(node->Name, wcsnlen_s(node->Name, 20), identity.mName);
                return identity;
            }
        }
    }
    std::copy_n(house.UIName, wcsnlen_s(house.UIName, 21), identity.mName);
    return identity;
}

ReplayPlayerIdentity PlayerIdentity(const HouseClass& house, const ReplayPlayerIdentities& identities) noexcept
{
    if (house.ArrayIndex >= 0 && static_cast<std::size_t>(house.ArrayIndex) < identities.size())
    {
        const auto& identity = identities[house.ArrayIndex];
        if (identity.mKnown) return identity;
    }
    return ReadPlayerIdentity(house);
}

viewer::PlayerBattleNumbers ReadBattleNumbers(const HouseClass& house, bool isHuman)
{
    viewer::PlayerBattleNumbers numbers;
    numbers.mUnitKills = house.TotalKilledUnits;
    numbers.mBuildingKills = house.TotalKilledBuildings;
    if (house.ArrayIndex >= 0 && house.ArrayIndex < 20)
    {
        // Native score tables are indexed by attacker and victim, separately for units and buildings.
        numbers.mUnitLosses = numbers.mBuildingLosses = 0;
        for (const auto* attacker : HouseClass::Array)
        {
            if (!attacker)
                continue;
            numbers.mUnitLosses += attacker->KilledUnitsOfHouses[house.ArrayIndex];
            numbers.mBuildingLosses += attacker->KilledBuildingsOfHouses[house.ArrayIndex];
        }
    }
    numbers.mProduced = {house.FactoryProducedInfantryTypes.GetTotal(), house.FactoryProducedUnitTypes.GetTotal(),
        house.FactoryProducedAircraftTypes.GetTotal(), house.FactoryProducedBuildingTypes.GetTotal()};
    numbers.mOwned = {house.OwnedInfantryTypes.GetTotal(), house.OwnedUnitTypes.GetTotal(),
        house.OwnedAircraftTypes.GetTotal(), house.OwnedBuildingTypes.GetTotal()};
    numbers.mHarvested = house.HarvestedCredits;
    numbers.mSpent = house.CreditsSpent;
    // Available_Money also includes the value of stored ore.
    numbers.mCash = house.Available_Money();
    numbers.mPowerOutput = house.PowerOutput;
    numbers.mPowerDrain = house.PowerDrain;
    if (isHuman) numbers.mFlags |= viewer::Human;
    if (house.Defeated) numbers.mFlags |= viewer::Defeated;
    if (house.IsWinner) numbers.mFlags |= viewer::Winner;
    if (house.IsLoser) numbers.mFlags |= viewer::Loser;
    return numbers;
}
}

struct ReplayWorldReader::Impl
{
    CameoReader mCameos;
};

ReplayWorldReader::ReplayWorldReader() : mImpl(std::make_unique<Impl>()) {}
ReplayWorldReader::~ReplayWorldReader() = default;

void ReplayWorldReader::Reset() noexcept
{
    mImpl->mCameos.Clear();
}

const IconStats& ReplayWorldReader::GetIconStats() const noexcept
{
    return mImpl->mCameos.GetStats();
}

ReplayPlayerIdentities ReadPlayerIdentities() noexcept
{
    ReplayPlayerIdentities identities{};
    const auto& houses = HouseClass::Array;
    if (houses.Count <= 0 || houses.Count > static_cast<int>(identities.size()) || !houses.Items)
        return identities;
    for (const auto* house : houses)
    {
        if (house && house->Type && !house->Type->MultiplayPassive &&
            house->ArrayIndex >= 0 && static_cast<std::size_t>(house->ArrayIndex) < identities.size())
            identities[house->ArrayIndex] = ReadPlayerIdentity(*house);
    }
    return identities;
}

BattleStats ReadBattleStats(const ReplayPlayerIdentities& identities) noexcept
{
    BattleStats result;
    const auto& houses = HouseClass::Array;
    if (houses.Count <= 0 || houses.Count > 64 || !houses.Items)
        return result;

    for (const auto* house : houses)
    {
        if (!house || !house->Type || house->Type->MultiplayPassive || house->ArrayIndex < 0 ||
            house->ArrayIndex >= 64)
            continue;
        const auto identity = PlayerIdentity(*house, identities);
        if (identity.mIsSpectator)
            continue;

        auto& row = result.mPlayers[result.mCount++];
        row.mHouse = house->ArrayIndex;
        std::copy_n(identity.mName, wcsnlen_s(identity.mName, 24), row.mName);
        if (house->Type->UIName)
            std::copy_n(house->Type->UIName, wcsnlen_s(house->Type->UIName, 47), row.mFaction);
        row.mNumbers = ReadBattleNumbers(*house, identity.mIsHuman);
        const auto& numbers = row.mNumbers;
        row.mKills = numbers.mUnitKills + numbers.mBuildingKills;
        row.mLosses = numbers.mUnitLosses < 0 ? -1 : numbers.mUnitLosses + numbers.mBuildingLosses;
        const auto& produced = numbers.mProduced;
        row.mProduced = produced.mInfantry + produced.mVehicles + produced.mAircraft + produced.mBuildings;
    }
    return result;
}

void ReplayWorldReader::Read(viewer::ReplayViewSnapshot& snapshot, const ReplayPlayerIdentities& identities)
{
    mImpl->mCameos.BeginSample();
    snapshot.mFrameNumber = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
    snapshot.mPlaybackSpeedSetting = GameOptionsClass::Instance.GameSpeed;
    const auto& window = DSurface::WindowBounds;
    const auto& sidebar = SidebarClass::Instance;
    const int tabIndex = std::clamp(sidebar.ActiveTabIndex, 0, 3);
    if (window.Width > 0 && window.Height > 0)
    {
        viewer::NativeUiLayout layout{window.Width, window.Height, {}, {}};
        const auto& view = DSurface::ViewBounds;
        const int viewBottom = view.Y + view.Height - window.Y;
        if (view.Width > 0 && view.Height > 0 && viewBottom > 0 && viewBottom < window.Height)
            layout.mFooter = {view.X - window.X, viewBottom, view.X + view.Width - window.X, window.Height};

        const auto& bounds = DSurface::SidebarBounds;
        int contentTop = sidebar.Tabs[tabIndex].Location.Y - window.Y;
        if (bounds.Width > 0 && bounds.Height > 0 && contentTop > 0 && contentTop < window.Height)
        {
            const auto& radar = sidebar.unknown_rect_149C;
            const int sidebarTop = bounds.Y - window.Y;
            const int sidebarBottom = sidebarTop + bounds.Height;
            // 雷达的坐标相对 Sidebar surface，建造条的坐标相对游戏窗口。
            const auto radarBottom = static_cast<std::int64_t>(sidebarTop) + radar.Y + radar.Height;
            if (radar.Height > 0 && radarBottom > sidebarTop && radarBottom <= sidebarBottom)
                contentTop = std::max(contentTop, static_cast<int>(radarBottom));
            layout.mSidebar = {bounds.X - window.X, std::max(contentTop, sidebarTop),
                bounds.X + bounds.Width - window.X, sidebarBottom};
        }
        snapshot.mNativeUiLayout = layout;
    }
    snapshot.mWasNetworkGame = SessionClass::Instance.GameMode == GameMode::LAN ||
        SessionClass::Instance.GameMode == GameMode::Internet;
    if (ScenarioClass::Instance)
    {
        snapshot.mMapName = Utf8(ScenarioClass::Instance->UINameLoaded, 0x2D);
        if (snapshot.mMapName.empty())
            snapshot.mMapName = Utf8(ScenarioClass::Instance->Name, 0x2D);
    }

    std::unordered_map<HouseClass*, std::size_t> playerIndices;
    const auto& houses = HouseClass::Array;
    if (houses.Count < 0 || houses.Count > 64 || (houses.Count > 0 && !houses.Items))
        throw std::runtime_error("Invalid House array during replay HUD sampling");
    for (int index = 0; index < houses.Count; ++index)
    {
        auto* house = houses.Items[index];
        if (!house || !house->Type || house->Type->MultiplayPassive)
            continue;
        const auto identity = PlayerIdentity(*house, identities);
        if (identity.mIsSpectator)
            continue;
        viewer::PlayerSummary player;
        player.mHouseIndex = house->ArrayIndex;
        player.mName = Utf8(identity.mName, 24);
        if (player.mName.empty())
            player.mName = "未识别玩家";
        player.mFaction = TypeName(house->Type);
        player.mColorRgb = (static_cast<std::uint32_t>(house->Color.R) << 16) |
            (static_cast<std::uint32_t>(house->Color.G) << 8) | house->Color.B;
        player.mIsHuman = identity.mIsHuman;
        player.mDefeated = house->Defeated;
        player.mBattleNumbers = ReadBattleNumbers(*house, identity.mIsHuman);
        const auto& numbers = *player.mBattleNumbers;
        player.mCredits = numbers.mCash;
        player.mPowerOutput = numbers.mPowerOutput;
        player.mPowerDrain = numbers.mPowerDrain;
        playerIndices.emplace(house, snapshot.mPlayers.size());
        snapshot.mPlayers.push_back(std::move(player));
    }

    std::unordered_map<FactoryClass*, std::string> factoryLabels;
    const auto& buildings = BuildingClass::Array;
    if (buildings.Count >= 0 && buildings.Count <= 100000 &&
        (buildings.Count == 0 || buildings.Items))
    {
        for (int index = 0; index < buildings.Count; ++index)
        {
            const auto* building = buildings.Items[index];
            if (building && building->Factory && building->Type)
                factoryLabels.try_emplace(building->Factory, TypeName(building->Type));
        }
    }

    const auto& factories = FactoryClass::Array;
    if (factories.Count >= 0 && factories.Count <= 100000 &&
        (factories.Count == 0 || factories.Items))
    {
        for (auto& player : snapshot.mPlayers)
            player.mHasProductionData = true;
        for (int index = 0; index < factories.Count; ++index)
        {
            const auto* factory = factories.Items[index];
            if (!factory || !factory->Object)
                continue;
            const auto playerIndex = playerIndices.find(factory->Owner);
            if (playerIndex == playerIndices.end())
                continue;
            viewer::ProductionLine line;
            line.mId = factory->UniqueID;
            const auto label = factoryLabels.find(const_cast<FactoryClass*>(factory));
            line.mLabel = label == factoryLabels.end() ? "生产线" : label->second;
            const auto* productType = factory->Object->GetTechnoType();
            line.mProductName = TypeName(productType);
            line.mIcon = mImpl->mCameos.Resolve(productType);
            line.mProgress = std::clamp(static_cast<float>(factory->Production.Value) / 54.0f, 0.0f, 1.0f);
            line.mReady = factory->Production.Value >= 54;
            line.mOnHold = !line.mReady && (factory->OnHold || factory->IsSuspended);
            line.mQueuedCount = static_cast<std::uint32_t>(std::max(0, factory->QueuedObjects.Count));
            snapshot.mPlayers[playerIndex->second].mProductionLines.push_back(std::move(line));
        }
    }

    std::vector<std::map<std::pair<int, std::string>, viewer::UnitSummary>> summaries(snapshot.mPlayers.size());
    const auto& technos = TechnoClass::Array;
    if (technos.Count >= 0 && technos.Count <= 100000 &&
        (technos.Count == 0 || technos.Items))
    {
        for (auto& player : snapshot.mPlayers)
            player.mHasUnitData = true;
        for (int index = 0; index < technos.Count; ++index)
        {
            auto* techno = technos.Items[index];
            if (!techno || !techno->IsAlive || !techno->IsOnMap || techno->InLimbo || techno->Health <= 0)
                continue;
            const auto playerIndex = playerIndices.find(techno->Owner);
            if (playerIndex == playerIndices.end())
                continue;
            const auto* type = techno->GetTechnoType();
            if (!type)
                continue;
            const auto category = Category(techno->WhatAmI());
            const std::string id(type->ID, strnlen_s(type->ID, sizeof(type->ID)));
            auto& summary = summaries[playerIndex->second][{static_cast<int>(category), id}];
            if (summary.mCount == 0)
            {
                summary.mTypeId = id;
                summary.mName = TypeName(type);
                summary.mCategory = category;
                summary.mIcon = mImpl->mCameos.Resolve(type);
            }
            ++summary.mCount;
        }
    }
    for (std::size_t index = 0; index < summaries.size(); ++index)
        for (auto& [key, summary] : summaries[index])
            snapshot.mPlayers[index].mUnits.push_back(std::move(summary));
}
}
