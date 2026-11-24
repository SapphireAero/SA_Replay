#include "ReplayViewExchange.h"

#include <algorithm>

namespace rareplay::viewer
{
void ReplayViewExchange::Publish(std::shared_ptr<const ReplayViewSnapshot> snapshot) noexcept
{
    mSnapshot.store(std::move(snapshot));
}

std::shared_ptr<const ReplayViewSnapshot> ReplayViewExchange::Read() const noexcept
{
    return mSnapshot.load();
}

void ReplayViewExchange::Submit(ViewerCommand command)
{
    std::lock_guard lock(mCommandMutex);
    // 速度滑块只需要最后一个值；限制队列，避免暂停期间无限积压。
    if (mCommands.size() >= 32)
        mCommands.erase(mCommands.begin());
    mCommands.push_back(command);
    mHasPauseCommand.store(std::any_of(mCommands.begin(), mCommands.end(),
        [](const ViewerCommand& queued) { return queued.mKind == ViewerCommandKind::SetPaused; }));
}

std::vector<ViewerCommand> ReplayViewExchange::Drain()
{
    std::lock_guard lock(mCommandMutex);
    std::vector<ViewerCommand> commands;
    commands.swap(mCommands);
    mHasPauseCommand.store(false);
    return commands;
}

void ReplayViewExchange::SetPauseState(PlaybackPauseState state) noexcept
{
    mPauseState.store(state);
}

void ReplayViewExchange::Clear()
{
    Publish(nullptr);
    std::lock_guard lock(mCommandMutex);
    mCommands.clear();
    mHasPauseCommand.store(false);
    SetPauseState(PlaybackPauseState::Running);
}

ReplayViewExchange& GetReplayViewExchange() noexcept
{
    static ReplayViewExchange exchange;
    return exchange;
}
}
