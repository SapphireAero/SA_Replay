// SPDX-License-Identifier: GPL-3.0-only

#include "ReplayBattleReport.h"

#include <Windows.h>
#include <array>
#include <fstream>
#include <string>

namespace rareplay::plugin
{
namespace
{
constexpr std::array<char, 8> Magic{'R','A','B','A','T','L','E','2'};
constexpr std::array<char, 8> PreviousMagic{'R','A','B','A','T','L','E','1'};
static_assert(sizeof(game_api::BattlePlayerStats) == 232);

template<typename T>
void Write(std::ostream& output, const T& value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
}

void SaveBattleReport(const std::filesystem::path& replay, std::uint32_t frames,
    std::int64_t recordedMilliseconds, const game_api::BattleStats& stats) noexcept
{
    try
    {
        if (frames == 0 || (stats.mCount == 0 && recordedMilliseconds < 0))
            return;
        std::array<char, 32> payloadHash{};
        std::ifstream archive(replay, std::ios::binary);
        archive.seekg(64);
        if (!archive.read(payloadHash.data(), payloadHash.size()))
            return;

        auto target = replay;
        target += L".battle-stats";
        if (recordedMilliseconds < 0)
        {
            std::ifstream previous(target, std::ios::binary);
            std::array<char, 8> magic{};
            std::array<char, 32> hash{};
            std::uint32_t oldFrames = 0;
            std::int64_t duration = -1;
            if (previous.read(magic.data(), magic.size()) && (magic == Magic || magic == PreviousMagic) &&
                previous.read(hash.data(), hash.size()) && hash == payloadHash &&
                previous.read(reinterpret_cast<char*>(&oldFrames), sizeof(oldFrames)) && oldFrames == frames &&
                previous.read(reinterpret_cast<char*>(&duration), sizeof(duration)))
                recordedMilliseconds = duration;
        }

        auto temporary = target;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        Write(output, Magic);
        Write(output, payloadHash);
        Write(output, frames);
        Write(output, recordedMilliseconds);
        Write(output, stats.mCount);
        for (std::uint32_t i = 0; i < stats.mCount; ++i)
            Write(output, stats.mPlayers[i]);
        output.close();
        if (!output || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
            DeleteFileW(temporary.c_str());
    }
    catch (...) {}
}
}
