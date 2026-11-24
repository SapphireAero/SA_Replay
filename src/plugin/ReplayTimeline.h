#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace rareplay::plugin
{
/** 仅与当前启动请求对应。槽位都是自然对齐的 32 位整数。 */
class ReplayTimeline
{
public:
    ~ReplayTimeline();
    void Open(const std::array<std::uint8_t, 16>& id) noexcept;
    void Begin() noexcept;
    std::optional<std::uint32_t> ReadTarget() noexcept;
    int ReadRate() const noexcept;
    void WriteRate(int rate) noexcept;
    bool ReadPaused() const noexcept;
    void WritePaused(bool paused) noexcept;
    bool CanPause() const noexcept { return mCanPause; }
    void RequestRestart(std::uint32_t target) noexcept;
    void Update(std::uint32_t completed, std::optional<std::uint32_t> target) noexcept;
    std::uint32_t TotalFrames() const noexcept { return mTotalFrames; }

private:
    volatile long* mSlots = nullptr;
    std::uint32_t mTotalFrames = 0;
    bool mCanPause = false;
};
}
