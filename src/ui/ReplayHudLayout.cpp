#include "ReplayHudLayout.h"

#include <algorithm>
#include <cmath>

namespace rareplay::ui
{
namespace
{
UiRect MapRect(const viewer::NativeUiRect& rect, const UiRect& canvas, int nativeWidth, int nativeHeight) noexcept
{
    const float scaleX = (canvas.mRight - canvas.mLeft) / nativeWidth;
    const float scaleY = (canvas.mBottom - canvas.mTop) / nativeHeight;
    return {
        canvas.mLeft + std::clamp(rect.mLeft, 0, nativeWidth) * scaleX,
        canvas.mTop + std::clamp(rect.mTop, 0, nativeHeight) * scaleY,
        canvas.mLeft + std::clamp(rect.mRight, 0, nativeWidth) * scaleX,
        canvas.mTop + std::clamp(rect.mBottom, 0, nativeHeight) * scaleY};
}

UiRect Intersect(const UiRect& a, const UiRect& b) noexcept
{
    return {std::max(a.mLeft, b.mLeft), std::max(a.mTop, b.mTop),
            std::min(a.mRight, b.mRight), std::min(a.mBottom, b.mBottom)};
}
}

ReplayHudRegions GetReplayHudRegions(const std::optional<viewer::NativeUiLayout>& layout,
    float displayWidth, float displayHeight, UiRect gameContentRect) noexcept
{
    if (!layout || layout->mCanvasWidth <= 0 || layout->mCanvasHeight <= 0 ||
        !std::isfinite(displayWidth) || !std::isfinite(displayHeight) ||
        displayWidth <= 0 || displayHeight <= 0)
        return {};

    const int nativeWidth = layout->mCanvasWidth;
    const int nativeHeight = layout->mCanvasHeight;
    const UiRect display{0, 0, displayWidth, displayHeight};
    if (gameContentRect.mLeft >= 0 && gameContentRect.mTop >= 0 &&
        gameContentRect.mLeft < gameContentRect.mRight && gameContentRect.mTop < gameContentRect.mBottom &&
        gameContentRect.mRight <= displayWidth && gameContentRect.mBottom <= displayHeight &&
        (gameContentRect.mLeft > 0 || gameContentRect.mTop > 0 ||
         gameContentRect.mRight < displayWidth || gameContentRect.mBottom < displayHeight))
    {
        return {MapRect(layout->mFooter, gameContentRect, nativeWidth, nativeHeight),
                MapRect(layout->mSidebar, gameContentRect, nativeWidth, nativeHeight)};
    }

    // 有的呈现器用整个客户区绘制带黑边的图像。未提供实际画布时，只使用两种投影都确认的保留区。
    const float scale = std::min(displayWidth / nativeWidth, displayHeight / nativeHeight);
    const float dx = (displayWidth - nativeWidth * scale) * 0.5f;
    const float dy = (displayHeight - nativeHeight * scale) * 0.5f;
    const UiRect fit{dx, dy, displayWidth - dx, displayHeight - dy};
    return {Intersect(MapRect(layout->mFooter, display, nativeWidth, nativeHeight),
                      MapRect(layout->mFooter, fit, nativeWidth, nativeHeight)),
            Intersect(MapRect(layout->mSidebar, display, nativeWidth, nativeHeight),
                      MapRect(layout->mSidebar, fit, nativeWidth, nativeHeight))};
}
}
