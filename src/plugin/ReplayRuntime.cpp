#include "ReplayRuntime.h"

#include "Diagnostics.h"
#include "ReplayConfig.h"
#include "ReplayTimeline.h"
#include "ReplayBattleReport.h"
#include "ReplayViewPublisher.h"
#include "OverlayDiagnosticsReporter.h"
#include "../hooks/graphics/ReplayOverlayHook.h"
#include "../viewer/ReplayViewExchange.h"
#include "../game_api/GameApi.h"
#include "../replay/ReplaySession.h"
#include "../storage/FrameReader.h"
#include "../storage/FrameWriter.h"
#include "../target/TargetImage.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>

namespace rareplay::plugin
{
namespace
{
/** 游戏线程上唯一的启动请求、领域会话及文件句柄所有者。 */
class ReplayRuntime
{
public:
    explicit ReplayRuntime(ReplayRequest request) : mRequest(std::move(request)), mSession(mGame) {}
    ~ReplayRuntime()
    {
        if (mRateTimer)
            CloseHandle(mRateTimer);
    }

    bool IsPlayback() const { return mRequest.mMode == ReplayMode::Play; }
    bool IsStarted() const { return mIsStarted; }
    bool IsRecordingActive() const
    {
        return mIsStarted && mRequest.mMode == ReplayMode::Record &&
            mSession.GetState() == replay::ReplayState::Recording;
    }
    bool ShouldBlockPlayerCommands() const
    {
        return IsPlayback() && mIsPlaybackSourceOpen && !mHasStartFailed;
    }
    bool TrySetPlaybackViewingSpeed(int setting)
    {
        return ShouldBlockPlayerCommands() && mGame.TrySetViewerPlaybackSpeed(setting);
    }
    bool TryGetPlaybackViewingSpeed(int& outSetting) const
    {
        return ShouldBlockPlayerCommands() && mGame.TryGetViewerPlaybackSpeed(outSetting);
    }
    bool ShouldHandlePlaybackViewingSpeed() const
    {
        return ShouldBlockPlayerCommands() && mGame.CanControlViewerPlaybackSpeed();
    }
    bool IsSeeking() const noexcept { return mSeekTarget.has_value(); }
    int PlaybackRate() const noexcept { return mPlaybackRate; }
    bool HasPauseRequest() const noexcept
    {
        return mTimeline.CanPause() && (mTimeline.ReadPaused() || viewer::GetReplayViewExchange().HasPauseCommand());
    }
    bool ShouldInterruptFrameWait() const noexcept
    {
        return !IsSeeking() && HasPauseRequest();
    }
    bool CanInterruptFrameWait() const noexcept { return mTimeline.CanPause() && !IsSeeking(); }

    std::chrono::microseconds NativeFrameInterval(int networkMillis, int offlineTicks) const noexcept
    {
        // 联机计时器使用毫秒，离线计时器使用 16 ms 的 tick。
        return std::chrono::microseconds(mGame.IsNetworkGame()
            ? std::clamp(networkMillis, 0, 1000) * 1000
            : std::clamp(offlineTicks, 0, 62) * 16000);
    }

    void WaitScaledFrame(int networkMillis, int offlineTicks) noexcept
    {
        if (mPlaybackRate == 1 || IsSeeking() || mSession.GetState() != replay::ReplayState::Playing)
            return;

        const auto interval = NativeFrameInterval(networkMillis, offlineTicks) / mPlaybackRate;
        if (interval != mRateInterval)
        {
            mRateInterval = interval;
            mRateDeadline = mFrameStarted;
        }
        // 补偿系统计时器的唤醒误差，长时间卡顿后最多补偿一帧。
        mRateDeadline = std::max(mRateDeadline, mFrameStarted - interval) + interval;
        while (!ShouldInterruptFrameWait())
        {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
                mRateDeadline - std::chrono::steady_clock::now());
            if (left.count() < 100)
                break;
            if (!mRateTimer)
            {
                mRateTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                    TIMER_MODIFY_STATE | SYNCHRONIZE);
                if (!mRateTimer)
                    mRateTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            }
            const auto slice = std::min(left, std::chrono::nanoseconds(std::chrono::milliseconds(4)));
            LARGE_INTEGER delay{};
            delay.QuadPart = -(slice.count() / 100);
            if (!mRateTimer || !SetWaitableTimer(mRateTimer, &delay, 0, nullptr, nullptr, FALSE) ||
                WaitForSingleObject(mRateTimer, 6) != WAIT_OBJECT_0)
                Sleep(1);
        }
    }

    void SamplePlaybackRate(int networkMillis, int offlineTicks) noexcept
    {
        if (mSession.GetState() != replay::ReplayState::Playing)
            return;
        const auto completed = mSession.GetCompletedPlaybackFrames();
        if (completed == mLastPacedFrame)
            return;
        mLastPacedFrame = completed;
        const auto now = std::chrono::steady_clock::now();
        if (mRateSampleStarted == std::chrono::steady_clock::time_point{})
        {
            mRateSampleStarted = now;
            return;
        }
        const auto nominal = NativeFrameInterval(networkMillis, offlineTicks);
        // 原生无等待时用 60 帧每游戏秒作为标尺。
        mNominalSampleTime += nominal.count() > 0 ? nominal : std::chrono::microseconds(16667);
        const auto elapsed = std::chrono::duration<double>(now - mRateSampleStarted);
        if (elapsed >= std::chrono::milliseconds(500))
        {
            const double actual = std::chrono::duration<double>(mNominalSampleTime) / elapsed;
            if (!mActualRate)
            {
                char message[120]{};
                std::snprintf(message, sizeof(message), "Replay rate sample: %.2fx (%s, selected %dx)",
                    actual, IsSeeking() ? "seeking" : "playing", mPlaybackRate);
                Log(message);
            }
            mActualRate = actual;
            mNominalSampleTime = {};
            mRateSampleStarted = now;
        }
    }

    void ResetRateSample() noexcept
    {
        mActualRate.reset();
        mRateSampleStarted = {};
        mNominalSampleTime = {};
    }

    bool WaitForResume()
    {
        if (mSession.GetState() != replay::ReplayState::Playing || !mTimeline.CanPause())
            return false;
        auto& exchange = viewer::GetReplayViewExchange();
        if (exchange.HasPauseCommand() && !ConsumeViewerCommands())
            return true;
        if (!mTimeline.ReadPaused())
        {
            exchange.SetPauseState(viewer::PlaybackPauseState::Running);
            return false;
        }
        if (IsSeeking())
        {
            exchange.SetPauseState(viewer::PlaybackPauseState::PauseAfterSeek);
            return false;
        }

        exchange.SetPauseState(viewer::PlaybackPauseState::Paused);
        ResetRateSample();

        // The completed frame remains available to the display while the game thread
        // services Windows messages and viewer commands without advancing the frame.
        auto nextDraw = std::chrono::steady_clock::time_point{};
        while (mGame.IsWorldReady() && mTimeline.ReadPaused() && !IsSeeking())
        {
            MSG message{};
            for (int i = 0; i < 64 && PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE); ++i)
            {
                if (message.message == WM_QUIT)
                {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    mGame.EndReplay();
                    return true;
                }
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }
            if (!mGame.IsWorldReady() || !ConsumeViewerCommands() || !mTimeline.ReadPaused() || IsSeeking())
                break;

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextDraw)
            {
                nextDraw = now + std::chrono::milliseconds(16);
                UpdateViewer();
                mGame.RenderPausedPlayback();
                // 呈现可以在游戏线程上提交继续命令，离开暂停前无需再等一轮。
                if ((exchange.HasPauseCommand() && !ConsumeViewerCommands()) || !mTimeline.ReadPaused())
                    break;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 4, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        exchange.SetPauseState(mTimeline.ReadPaused() && IsSeeking()
            ? viewer::PlaybackPauseState::PauseAfterSeek : viewer::PlaybackPauseState::Running);
        return true;
    }

    bool OpenPlaybackSource()
    {
        if (!IsPlayback() || mHasStartFailed)
            return false;
        if (mIsPlaybackSourceOpen)
            return true;
        std::vector<std::uint8_t> metadata;
        std::string error;
        if (!mReader.Open(mRequest.mReplayPath.wstring(), target::TARGET_SHA256_BYTES,
                metadata, error))
            return Fail(error);
        if (!MatchesArchiveManifest(mRequest, metadata))
            return Fail("Replay request manifest differs from archive");
        if (!mReader.GetSessionInfo(mRecordedInfo, error))
            return Fail(error);
        mIsPlaybackSourceOpen = true;
        return true;
    }

    bool TryGetPlaybackSeed(std::uint32_t& outSeed)
    {
        if (!OpenPlaybackSource())
            return false;
        outSeed = mRecordedInfo.mRandomSeed;
        return true;
    }

    void BeginFrame()
    {
        mGame.BeginFrame();
        if (!mGame.IsWorldReady())
            return;
        if (!mIsStarted && !Start())
            return;
        if (mIsRestarting || !ConsumeViewerCommands())
            return;
        // 覆盖上一帧等待已经结束、这一帧还没开始模拟的短暂窗口。
        if (mTimeline.ReadPaused() && !IsSeeking() && mSession.GetCompletedPlaybackFrames() != 0)
        {
            WaitForResume();
            if (mIsRestarting || !mGame.IsWorldReady())
                return;
        }
        if (mPlaybackRate > 1)
        {
            mFrameStarted = std::chrono::steady_clock::now();
            if (IsSeeking())
                mRateDeadline = mFrameStarted;
        }
        const auto previousState = mSession.GetState();
        ReportFailure(mSession.OnFrameBegin());
        if (previousState == replay::ReplayState::Playing &&
            mSession.GetState() == replay::ReplayState::Finished)
        {
            SaveBattleReport(mRequest.mReplayPath,
                static_cast<std::uint32_t>(mSession.GetCompletedPlaybackFrames()), -1,
                game_api::ReadBattleStats(mPlayerIdentities));
            Log("Replay playback reached EOF");
        }
    }

    void BeforeNativeEvents()
    {
        if (!mIsStarted || mRequest.mMode != ReplayMode::Record)
            return;
        mGame.BeforeNativeEventExecution();
        ReportFailure(mSession.OnBeforeEvents());
    }

    void BeforeSimulation()
    {
        if (mIsStarted && IsPlayback() &&
            mSession.GetState() == replay::ReplayState::Playing)
            ReportFailure(mSession.OnBeforeSimulation());
    }

    void CapturePlayerInputEffect(const replay::RecordedInputEffect& effect)
    {
        if (!IsRecordingActive())
            return;
        std::string error;
        if (!mGame.RecordPlayerInputEffect(effect, error))
            FailPlayerInputCapture(error);
    }

    void FailPlayerInputCapture(const std::string& error)
    {
        if (!IsRecordingActive())
            return;
        mSession.Stop();
        Fail(error);
    }

    void CompleteFrame()
    {
        if (mIsStarted && mRequest.mMode == ReplayMode::Record)
            ReportFailure(mSession.OnFrameEnd());
        if (mIsStarted && IsPlayback() && mSession.GetState() == replay::ReplayState::Playing)
        {
            const auto completed = static_cast<std::uint32_t>(mSession.GetCompletedPlaybackFrames());
            if (mSeekTarget && completed > *mSeekTarget)
            {
                mSeekTarget.reset();
                ResetRateSample();
            }
            mTimeline.Update(completed, mSeekTarget);
            UpdateViewer();
        }
    }

    bool ProcessPlaybackFrame()
    {
        if (!IsPlayback())
            return false;
        if (mIsRestarting)
            return true;
        if (!mIsStarted)
        {
            if (!mGame.IsWorldReady() || !Start())
            {
                mGame.EndReplay();
                return true;
            }
        }
        mGame.DiscardLocalCommands();
        mSession.ProcessPlaybackFrame();
        ReportFailure(mSession.GetState() != replay::ReplayState::Failed);
        return true;
    }

    void Finish()
    {
        if (IsPlayback())
            mOverlayDiagnostics.Report(true);
        mViewPublisher.End();
        if (mIsStarted && mSession.GetState() == replay::ReplayState::Recording)
        {
            const auto stats = game_api::ReadBattleStats(mPlayerIdentities);
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - mRecordingStarted).count();
            const bool success = mSession.Finish();
            ReportFailure(success);
            if (success)
            {
                SaveBattleReport(mRequest.mReplayPath,
                    static_cast<std::uint32_t>(mSession.GetCompletedPlaybackFrames()), duration, stats);
                Log("Replay recording finalized");
            }
        }
        else
            mSession.Stop();
        mReader.Close();
    }

    void FailUnexpected()
    {
        mViewPublisher.End();
        mSession.Stop();
        if (IsPlayback())
            mGame.EndReplay();
        Log("Unexpected exception in replay hook");
    }

private:
    bool ConsumeViewerCommands()
    {
        if (!IsPlayback())
            return true;
        auto target = mTimeline.ReadTarget();
        try
        {
            for (const auto& command : viewer::GetReplayViewExchange().Drain())
            {
                if (command.mSessionId != mViewPublisher.GetSessionId())
                    continue;
                if (command.mKind == viewer::ViewerCommandKind::SetPlaybackSpeed)
                    TrySetPlaybackViewingSpeed(command.mValue);
                if (command.mKind == viewer::ViewerCommandKind::SetPlaybackRate)
                    mTimeline.WriteRate(command.mValue);
                if (command.mKind == viewer::ViewerCommandKind::SetPaused)
                    mTimeline.WritePaused(command.mValue != 0);
                if (command.mKind == viewer::ViewerCommandKind::Seek && command.mValue >= 0 &&
                    static_cast<std::uint32_t>(command.mValue) < mTimeline.TotalFrames())
                    target = static_cast<std::uint32_t>(command.mValue);
            }
        }
        catch (...) { Log("Replay HUD command was ignored after an exception"); }
        const int rate = mTimeline.ReadRate();
        if (rate != mPlaybackRate)
        {
            mPlaybackRate = rate;
            mRateInterval = std::chrono::microseconds::zero();
            ResetRateSample();
        }
        if (!target)
            return true;

        const auto completed = mSession.GetCompletedPlaybackFrames();
        const auto requested = static_cast<std::uint64_t>(*target) + 1;
        char message[120]{};
        std::snprintf(message, sizeof(message), "Replay seek: target frame %u, completed %llu%s", *target + 1,
            static_cast<unsigned long long>(completed), requested < completed ? "; restarting" : "");
        Log(message);
        ResetRateSample();
        if (requested < completed)
        {
            mIsRestarting = true;
            mSeekTarget.reset();
            mTimeline.RequestRestart(*target);
            mSession.Stop();
            mGame.EndReplay();
            return false;
        }
        mSeekTarget = requested > completed ? target : std::nullopt;
        return true;
    }

    void UpdateViewer() noexcept
    {
        // 界面失败只能停止界面，不能把已验证的回放会话转换为 Failed。
        if (mHasViewFailed)
            return;
        try
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= mNextOverlayProbe)
            {
                mNextOverlayProbe = now + std::chrono::milliseconds(500);
                hooks::EnsureReplayOverlayStarted();
                mOverlayDiagnostics.Report();
            }
            // 呈现回调在读取快照之前就会记录可用画面。GDI 等不可绘制路径
            // 无需在游戏模拟线程统计单位和解码图标。
            if (!hooks::HasReplayOverlayPresenter())
                return;
            int speed = 0;
            const std::optional<int> override = mGame.TryGetViewerPlaybackSpeed(speed)
                ? std::optional<int>(speed) : std::nullopt;
            mViewPublisher.Publish(override, mGame.CanControlViewerPlaybackSpeed(), mTimeline.TotalFrames(),
                mSeekTarget, mPlaybackRate, mActualRate, mTimeline.CanPause(),
                static_cast<std::uint32_t>(mSession.GetCompletedPlaybackFrames()), mGame.GetPlaybackCommandCounts(),
                mPlayerIdentities);
        }
        catch (...)
        {
            mHasViewFailed = true;
            mViewPublisher.End();
            Log("Replay HUD disabled after a snapshot exception; playback continues");
        }
    }

    bool Start()
    {
        if (mHasStartFailed)
            return false;
        replay::ReplaySessionInfo info;
        std::string error;
        if (!mGame.ReadSessionInfo(info, error))
            return Fail(error);
        if (IsPlayback())
        {
            if (!OpenPlaybackSource())
                return false;
            if (!mSession.StartPlayback(mReader))
            {
                return Fail(mSession.GetLastError());
            }
        }
        else if (!mWriter.Open(mRequest.mReplayPath.wstring(), target::TARGET_SHA256_BYTES,
                mRequest.mManifest, info, error) || !mSession.StartRecording(mWriter))
        {
            return Fail(error.empty() ? mSession.GetLastError() : error);
        }
        mPlayerIdentities = game_api::ReadPlayerIdentities();
        mIsStarted = true;
        if (!IsPlayback())
            mRecordingStarted = std::chrono::steady_clock::now();
        Log(IsPlayback() ? "Replay playback started" : "Replay recording started");
        if (IsPlayback())
        {
            Log("Replay viewer command filtering active; native map input remains available");
            mTimeline.Open(mRequest.mRequestId);
            mTimeline.Begin();
            try
            {
                const auto name = mRequest.mReplayPath.filename().u8string();
                mViewPublisher.Begin(std::string(name.begin(), name.end()),
                    static_cast<int>(mRecordedInfo.mLocalHouseIndex));
            }
            catch (...) { mHasViewFailed = true; Log("Replay HUD initialization failed; playback continues"); }
        }
        return true;
    }

    bool Fail(const std::string& error)
    {
        mViewPublisher.End();
        if (!mIsStarted)
            mHasStartFailed = true;
        if (!mHasLoggedFailure)
        {
            Log(error.c_str());
            mHasLoggedFailure = true;
        }
        if (IsPlayback())
            mGame.EndReplay();
        else
            mWriter.Abort();
        return false;
    }

    void ReportFailure(bool success)
    {
        if (!success)
            Fail(mSession.GetLastError());
    }

    ReplayRequest mRequest;
    game_api::GameApi mGame;
    storage::FrameReader mReader;
    storage::FrameWriter mWriter;
    replay::ReplaySession mSession;
    replay::ReplaySessionInfo mRecordedInfo;
    ReplayViewPublisher mViewPublisher;
    game_api::ReplayPlayerIdentities mPlayerIdentities;
    ReplayTimeline mTimeline;
    std::optional<std::uint32_t> mSeekTarget;
    int mPlaybackRate = 1;
    std::chrono::steady_clock::time_point mFrameStarted{};
    std::chrono::steady_clock::time_point mRecordingStarted{};
    std::chrono::steady_clock::time_point mRateDeadline{};
    std::chrono::microseconds mRateInterval{};
    std::chrono::steady_clock::time_point mRateSampleStarted{};
    std::chrono::microseconds mNominalSampleTime{};
    std::uint64_t mLastPacedFrame = 0;
    std::optional<double> mActualRate;
    HANDLE mRateTimer = nullptr;
    bool mIsRestarting = false;
    bool mHasViewFailed = false;
    std::chrono::steady_clock::time_point mNextOverlayProbe{};
    OverlayDiagnosticsReporter mOverlayDiagnostics;
    bool mIsStarted = false;
    bool mIsPlaybackSourceOpen = false;
    bool mHasStartFailed = false;
    bool mHasLoggedFailure = false;
};

std::unique_ptr<ReplayRuntime> g_Runtime;
bool g_StartupAttempted = false;
}

void OnStartup() noexcept
{
    if (g_StartupAttempted)
        return;
    g_StartupAttempted = true;
    try
    {
        target::TargetImage image;
        std::string error;
        const bool isTargetValid = image.LoadCurrentProcess(error);
        const auto gameDirectory = image.GetPath().empty()
            ? std::filesystem::current_path() : image.GetPath().parent_path();
        std::string diagnosticError;
        if (!OpenBootstrapDiagnostics(gameDirectory, diagnosticError))
            Log(diagnosticError.c_str());
        Log("Replay bootstrap entered");
        if (!isTargetValid)
        {
            Log("Target validation failed");
            Log(error.c_str());
            return;
        }
        const auto markerPath = gameDirectory / L"rareplay-runtime.bin";
        ReplayRequest request;
        std::vector<std::uint8_t> rawBytes;
        if (!ReadReplayRequest(markerPath, request, rawBytes, error))
        {
            Log("Replay request read failed");
            Log(error.c_str());
            return;
        }
        if (!ConsumeReplayRequest(markerPath, rawBytes, error))
        {
            Log("Replay request consumption failed");
            Log(error.c_str());
            return;
        }
        g_Runtime = std::make_unique<ReplayRuntime>(std::move(request));
        Log("Replay request consumed");
        if (g_Runtime->IsPlayback())
            g_Runtime->OpenPlaybackSource();
    }
    catch (...)
    {
        Log("Replay startup failed");
    }
}

bool TryGetPlaybackSeed(std::uint32_t& outSeed) noexcept
{
    OnStartup();
    try { return g_Runtime && g_Runtime->TryGetPlaybackSeed(outSeed); }
    catch (...) { OnHookFailure(); return false; }
}

void OnFrameBegin() noexcept
{
    OnStartup();
    try { if (g_Runtime) g_Runtime->BeginFrame(); }
    catch (...) { OnHookFailure(); }
}

void OnBeforeSimulation() noexcept
{
    try { if (g_Runtime) g_Runtime->BeforeSimulation(); }
    catch (...) { OnHookFailure(); }
}

bool ShouldCapturePlayerInputEffect() noexcept
{
    return g_Runtime && g_Runtime->IsRecordingActive();
}

void OnPlayerInputEffectCaptured(const replay::RecordedInputEffect& effect) noexcept
{
    try { if (g_Runtime) g_Runtime->CapturePlayerInputEffect(effect); }
    catch (...) { OnHookFailure(); }
}

void OnPlayerInputEffectCaptureFailed() noexcept
{
    try
    {
        if (g_Runtime)
            g_Runtime->FailPlayerInputCapture("Unsupported movement feedback capture parameters");
    }
    catch (...) { OnHookFailure(); }
}

bool ShouldBlockPlayerCommands() noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->ShouldBlockPlayerCommands();
}

bool TrySetPlaybackViewingSpeed(int setting) noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->TrySetPlaybackViewingSpeed(setting);
}

bool TryGetPlaybackViewingSpeed(int& outSetting) noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->TryGetPlaybackViewingSpeed(outSetting);
}

bool ShouldHandlePlaybackViewingSpeed() noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->ShouldHandlePlaybackViewingSpeed();
}

bool IsReplaySeeking() noexcept
{
    return g_Runtime && g_Runtime->IsSeeking();
}

int GetReplayPlaybackRate() noexcept
{
    return g_Runtime ? g_Runtime->PlaybackRate() : 1;
}

void WaitScaledReplayFrame(int networkMillis, int offlineTicks) noexcept
{
    if (g_Runtime)
        g_Runtime->WaitScaledFrame(networkMillis, offlineTicks);
}

bool PauseAtReplayFrameBoundary() noexcept
{
    try { return g_Runtime && g_Runtime->WaitForResume(); }
    catch (...) { OnHookFailure(); return false; }
}

bool ShouldInterruptReplayFrameWait() noexcept
{
    return g_Runtime && g_Runtime->ShouldInterruptFrameWait();
}

bool CanInterruptReplayFrameWait() noexcept
{
    return g_Runtime && g_Runtime->CanInterruptFrameWait();
}

void FinishReplayFrameWait(int networkMillis, int offlineTicks) noexcept
{
    try
    {
        if (g_Runtime)
        {
            g_Runtime->SamplePlaybackRate(networkMillis, offlineTicks);
            g_Runtime->WaitForResume();
        }
    }
    catch (...) { OnHookFailure(); }
}

void OnBeforeNativeEvents() noexcept
{
    try { if (g_Runtime) g_Runtime->BeforeNativeEvents(); }
    catch (...) { OnHookFailure(); }
}

void OnFrameComplete() noexcept
{
    try { if (g_Runtime) g_Runtime->CompleteFrame(); }
    catch (...) { OnHookFailure(); }
}

bool OnPlaybackParser() noexcept
{
    try { return g_Runtime && g_Runtime->ProcessPlaybackFrame(); }
    catch (...) { OnHookFailure(); return g_Runtime && g_Runtime->IsPlayback(); }
}

bool ShouldBypassPeerProgressWait() noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->IsPlayback();
}

void OnSessionEnd() noexcept
{
    try
    {
        if (g_Runtime)
        {
            g_Runtime->Finish();
            g_Runtime.reset();
        }
        CloseDiagnostics();
        g_StartupAttempted = false;
    }
    catch (...) { OnHookFailure(); }
}

void OnHookFailure() noexcept
{
    if (g_Runtime)
        g_Runtime->FailUnexpected();
}
}
