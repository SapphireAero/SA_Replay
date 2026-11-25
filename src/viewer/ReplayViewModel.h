// SPDX-License-Identifier: GPL-3.0-only
// Modified by SA_Replay contributors. First published with these changes on 2026-11-24.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rareplay::viewer
{
enum class UnitCategory { Infantry, Vehicle, Aircraft, Building, Other };

/** 图像是独立的 RGBA 像素副本；不持有游戏对象或图形设备指针。 */
struct IconImage
{
    std::string mKey;
    int mWidth = 0;
    int mHeight = 0;
    std::vector<std::uint8_t> mRgba;
};

struct UnitSummary
{
    std::string mTypeId;
    std::string mName;
    UnitCategory mCategory = UnitCategory::Other;
    std::uint32_t mCount = 0;
    std::shared_ptr<const IconImage> mIcon;
};

struct ProductionLine
{
    std::uint32_t mId = 0;
    std::string mLabel;
    std::string mProductName;
    std::shared_ptr<const IconImage> mIcon;
    float mProgress = 0.0f;
    bool mOnHold = false;
    bool mReady = false;
    std::uint32_t mQueuedCount = 0;
};

struct BattleUnitCounts
{
    std::int32_t mInfantry = 0;
    std::int32_t mVehicles = 0;
    std::int32_t mAircraft = 0;
    std::int32_t mBuildings = 0;
};

enum BattlePlayerFlag : std::uint32_t { Human = 1, Defeated = 2, Winner = 4, Loser = 8 };

struct PlayerBattleNumbers
{
    std::int32_t mUnitKills = 0;
    std::int32_t mBuildingKills = 0;
    std::int32_t mUnitLosses = -1;
    std::int32_t mBuildingLosses = -1;
    BattleUnitCounts mProduced;
    BattleUnitCounts mOwned;
    std::int32_t mHarvested = 0;
    std::int32_t mSpent = 0;
    std::int32_t mCash = 0;
    std::int32_t mPowerOutput = 0;
    std::int32_t mPowerDrain = 0;
    std::uint32_t mFlags = 0;
};

using ReplayCommandCounts = std::array<std::uint32_t, 64>;

struct PlayerSummary
{
    int mHouseIndex = -1;
    std::string mName;
    std::string mFaction;
    std::uint32_t mColorRgb = 0xD4A753;
    bool mIsHuman = false;
    bool mDefeated = false;
    std::optional<std::int64_t> mCredits;
    std::optional<int> mPowerOutput;
    std::optional<int> mPowerDrain;
    std::optional<PlayerBattleNumbers> mBattleNumbers;
    std::optional<std::uint32_t> mCommands;
    bool mHasProductionData = false;
    bool mHasUnitData = false;
    std::vector<ProductionLine> mProductionLines;
    std::vector<UnitSummary> mUnits;
};

struct ControlGroup
{
    int mNumber = 0;
    std::vector<UnitSummary> mUnits;
};

struct FpsSample
{
    std::uint32_t mFrameNumber = 0;
    float mFps = 0.0f;
};

struct NativeUiRect
{
    int mLeft = 0;
    int mTop = 0;
    int mRight = 0;
    int mBottom = 0;
};

/** 由游戏线程采样的原生保留区，坐标相对游戏画布。 */
struct NativeUiLayout
{
    int mCanvasWidth = 0;
    int mCanvasHeight = 0;
    NativeUiRect mFooter;
    NativeUiRect mSidebar;
};

/** 游戏线程发布的只读值快照；缺失数据用 optional/能力标志表示，不能当作零。 */
struct ReplayViewSnapshot
{
    std::uint64_t mSessionId = 0;
    bool mActive = false;
    bool mIsPreview = false;
    std::string mReplayName;
    std::string mMapName;
    std::string mMapHash;
    std::optional<std::int64_t> mStartedUtc;
    bool mWasNetworkGame = false;
    std::uint32_t mFrameNumber = 0;
    std::uint32_t mCompletedFrames = 0;
    std::optional<std::uint32_t> mTotalFrames;
    std::optional<std::uint32_t> mSeekTarget;
    int mPlaybackRate = 1;
    std::optional<double> mActualPlaybackRate;
    bool mCanPause = false;
    int mRecordedLocalHouseIndex = -1;
    int mPlaybackSpeedSetting = 0;
    bool mCanChangePlaybackSpeed = false;
    bool mHasPlayerVision = false;
    bool mHasRecordedGroups = false;
    std::optional<NativeUiLayout> mNativeUiLayout;
    std::vector<PlayerSummary> mPlayers;
    std::vector<ControlGroup> mRecordedGroups;
    std::vector<FpsSample> mRecordedFps;
};

enum class ViewerCommandKind { SetPlaybackSpeed, Seek, SetPlaybackRate, SetPaused };
struct ViewerCommand
{
    std::uint64_t mSessionId = 0;
    ViewerCommandKind mKind = ViewerCommandKind::SetPlaybackSpeed;
    int mValue = 0;
};
}
