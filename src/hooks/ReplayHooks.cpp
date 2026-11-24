#include "../game_api/GameApi.h"
#include "../plugin/ReplayRuntime.h"
#include "../target/TargetAbi.h"
#include "ViewerInputScope.h"

#include <Syringe.h>

// Syringe 从 .syhks00 读取声明；CALL rel32 站点由回调显式调用原目标并跳到续点。
DEFINE_HOOK(0x52D9A0, ReplayStartupHook, 6)
{
    rareplay::plugin::OnStartup();
    return 0;
}

// 两个分支均已完成选种；在原指令读取种子并初始化 RNG 前恢复录像值。
DEFINE_HOOK(0x52FDF9, ReplayRandomSeedHook, 6)
{
    std::uint32_t seed = 0;
    if (rareplay::plugin::TryGetPlaybackSeed(seed))
        rareplay::game_api::GameApi::RestoreRandomSeedForInitialization(seed);
    return 0;
}

DEFINE_HOOK_AGAIN(0x52FE5C, ReplayRandomSeedHook, 5)

DEFINE_HOOK(0x55D360, ReplayFrameBeginHook, 5)
{
    rareplay::plugin::OnFrameBegin();
    return 0;
}

// Native Input 之后、Logic.Update 之前重演本帧输入效果。
DEFINE_HOOK(0x55DC99, ReplayBeforeSimulationHook, 5)
{
    rareplay::plugin::OnBeforeSimulation();
    return 0;
}

// 已安装 Syringe 至少改写 5 字节；覆盖 SUB ESP,40h + PUSH EBX + 完整 MOV EBX,[A80238]。
DEFINE_HOOK(0x64C380, ReplayBeforeEventsHook, 10)
{
    rareplay::plugin::OnBeforeNativeEvents();
    return 0;
}

DEFINE_HOOK(0x55DE40, ReplayParserHook, 5)
{
    if (rareplay::plugin::OnPlaybackParser())
        return static_cast<DWORD>(rareplay::target::FRAME_PARSER_CONTINUE);
    rareplay::game_api::GameApi::InvokeNativeFrameParser();
    return static_cast<DWORD>(rareplay::target::FRAME_PARSER_CONTINUE);
}

DEFINE_HOOK(0x55DE7E, ReplayFrameCompleteHook, 9)
{
    rareplay::plugin::OnFrameComplete();
    return 0;
}

DEFINE_HOOK(0x55DE9A, ReplayTimelineWaitHook, 5)
{
    using namespace rareplay::target;
    const auto wait = reinterpret_cast<void(__cdecl*)()>(FRAME_WAIT);
    const int networkMillis = *reinterpret_cast<const int*>(NETWORK_FRAME_WAIT_DURATION);
    const int offlineTicks = *reinterpret_cast<const int*>(OFFLINE_FRAME_WAIT_DURATION);
    const bool servicedPause = rareplay::plugin::PauseAtReplayFrameBoundary();
    if (servicedPause || rareplay::plugin::IsReplaySeeking() || rareplay::plugin::GetReplayPlaybackRate() > 1)
    {
        struct TemporaryWait
        {
            int& ticks;
            int saved;
            explicit TemporaryWait(std::uintptr_t address) : ticks(*reinterpret_cast<int*>(address)), saved(ticks)
            {
                ticks = 0;
            }
            ~TemporaryWait() { ticks = saved; }
        };
        {
            // 只调整帧间等待，原生计时器设定、游戏速度和录像事件均保留。
            TemporaryWait network(NETWORK_FRAME_WAIT_DURATION);
            TemporaryWait offline(OFFLINE_FRAME_WAIT_DURATION);
            wait();
        }
        if (!servicedPause)
            rareplay::plugin::WaitScaledReplayFrame(networkMillis, offlineTicks);
    }
    else
        wait();
    rareplay::plugin::FinishReplayFrameWait(networkMillis, offlineTicks);
    return static_cast<DWORD>(FRAME_WAIT_CONTINUE);
}

// 原生联机等待循环每轮都会回到这里，暂停时走原生收尾路径。
DEFINE_HOOK(0x55E1C2, ReplayNetworkWaitPauseHook, 5)
{
    return rareplay::plugin::ShouldInterruptReplayFrameWait()
        ? static_cast<DWORD>(rareplay::target::FRAME_WAIT_EPILOGUE) : 0;
}

// PUSH ESI + CALL EDI (Sleep) + JMP 回原生计时循环，保留循环自身的到期判断。
DEFINE_HOOK(0x55E32F, ReplayOfflineWaitPauseHook, 5)
{
    using namespace rareplay::target;
    if (rareplay::plugin::ShouldInterruptReplayFrameWait())
        return static_cast<DWORD>(FRAME_WAIT_EPILOGUE);
    const auto sleep = reinterpret_cast<void(WINAPI*)(DWORD)>(R->EDI());
    const DWORD delay = R->ESI();
    sleep(rareplay::plugin::CanInterruptReplayFrameWait() && delay > 4 ? 4 : delay);
    return static_cast<DWORD>(FRAME_WAIT_OFFLINE_LOOP);
}

DEFINE_HOOK_AGAIN(0x55E336, ReplayOfflineWaitPauseHook, 5)

DEFINE_HOOK(0x684B3E, ReplayPreworldWaitHook, 5)
{
    const bool ready = rareplay::plugin::ShouldBypassPeerProgressWait() ||
        rareplay::game_api::GameApi::InvokeNativePreworldWait();
    R->EAX(static_cast<DWORD>(ready));
    return static_cast<DWORD>(rareplay::target::PREWORLD_WAIT_CONTINUE);
}

DEFINE_HOOK(0x48CEAF, ReplaySessionEndHook, 5)
{
    rareplay::hooks::GetViewerInputScope().Reset();
    rareplay::plugin::OnSessionEnd();
    return 0;
}
