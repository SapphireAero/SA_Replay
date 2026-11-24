#include "ReplayTimeline.h"

#include <Windows.h>

namespace rareplay::plugin
{
namespace
{
constexpr long Magic = 0x33544c52, LegacyMagic = 0x32544c52;
constexpr int Total = 1, Completed = 2, Target = 3, Restart = 4, Seeking = 5, Rate = 6, Paused = 7;

constexpr bool IsValidRate(int rate) noexcept
{
    return rate == 1 || rate == 2 || rate == 4 || rate == 8 || rate == 16;
}
}

ReplayTimeline::~ReplayTimeline()
{
    if (mSlots)
        UnmapViewOfFile(const_cast<long*>(mSlots));
}

void ReplayTimeline::Open(const std::array<std::uint8_t, 16>& id) noexcept
{
    wchar_t name[] = L"Local\\RAReplayTimeline-00000000000000000000000000000000";
    constexpr wchar_t Hex[] = L"0123456789abcdef";
    auto* digits = name + 23;
    for (const auto byte : id)
    {
        *digits++ = Hex[byte >> 4];
        *digits++ = Hex[byte & 15];
    }

    HANDLE mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, name);
    if (!mapping)
        return;
    auto* slots = static_cast<volatile long*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0));
    CloseHandle(mapping);
    if (!slots)
        return;
    const auto count = InterlockedCompareExchange(slots + Total, 0, 0);
    const long version = InterlockedCompareExchange(slots, 0, 0);
    if ((version != Magic && version != LegacyMagic) || count <= 0)
    {
        UnmapViewOfFile(const_cast<long*>(slots));
        return;
    }
    mSlots = slots;
    mTotalFrames = static_cast<std::uint32_t>(count);
    mCanPause = version == Magic;
}

void ReplayTimeline::Begin() noexcept
{
    if (mSlots)
        InterlockedExchange(mSlots + Completed, 0);
}

std::optional<std::uint32_t> ReplayTimeline::ReadTarget() noexcept
{
    if (!mSlots)
        return std::nullopt;
    const long target = InterlockedExchange(mSlots + Target, -1);
    if (target < 0 || static_cast<std::uint32_t>(target) >= mTotalFrames)
        return std::nullopt;
    return static_cast<std::uint32_t>(target);
}

int ReplayTimeline::ReadRate() const noexcept
{
    const int rate = mSlots ? InterlockedCompareExchange(mSlots + Rate, 0, 0) : 1;
    return IsValidRate(rate) ? rate : 1;
}

void ReplayTimeline::WriteRate(int rate) noexcept
{
    if (mSlots && IsValidRate(rate))
        InterlockedExchange(mSlots + Rate, rate);
}

bool ReplayTimeline::ReadPaused() const noexcept
{
    return mCanPause && InterlockedCompareExchange(mSlots + Paused, 0, 0) == 1;
}

void ReplayTimeline::WritePaused(bool paused) noexcept
{
    if (mCanPause)
        InterlockedExchange(mSlots + Paused, paused ? 1 : 0);
}

void ReplayTimeline::RequestRestart(std::uint32_t target) noexcept
{
    if (mSlots)
        InterlockedExchange(mSlots + Restart, static_cast<long>(target));
}

void ReplayTimeline::Update(std::uint32_t completed, std::optional<std::uint32_t> target) noexcept
{
    if (!mSlots)
        return;
    InterlockedExchange(mSlots + Seeking, target ? static_cast<long>(*target) : -1);
    InterlockedExchange(mSlots + Completed, static_cast<long>(completed));
}
}
