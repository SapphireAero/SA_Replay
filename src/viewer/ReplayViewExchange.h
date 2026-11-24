#pragma once

#include "ReplayViewModel.h"

#include <atomic>
#include <mutex>

namespace rareplay::viewer
{
enum class PlaybackPauseState { Running, PauseAfterSeek, Paused };

/** 游戏线程与渲染线程仅交换不可变快照和有界命令；渲染线程不得访问 YRpp。 */
class ReplayViewExchange final
{
public:
    void Publish(std::shared_ptr<const ReplayViewSnapshot> snapshot) noexcept;
    std::shared_ptr<const ReplayViewSnapshot> Read() const noexcept;
    void Submit(ViewerCommand command);
    std::vector<ViewerCommand> Drain();
    bool HasPauseCommand() const noexcept { return mHasPauseCommand.load(); }
    PlaybackPauseState ReadPauseState() const noexcept { return mPauseState.load(); }
    void SetPauseState(PlaybackPauseState state) noexcept;
    void Clear();

private:
    std::atomic<std::shared_ptr<const ReplayViewSnapshot>> mSnapshot;
    std::mutex mCommandMutex;
    std::vector<ViewerCommand> mCommands;
    std::atomic<bool> mHasPauseCommand{false};
    std::atomic<PlaybackPauseState> mPauseState{PlaybackPauseState::Running};
};

ReplayViewExchange& GetReplayViewExchange() noexcept;
}
