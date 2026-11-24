#include "../src/ui/ReplayHudLayout.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
}

int main()
{
    try
    {
        using rareplay::ui::GetReplayHudRegions;
        using rareplay::viewer::NativeUiLayout;
        const NativeUiLayout layout{2560, 1440, {0, 1408, 2392, 1440}, {2392, 250, 2560, 1440}};
        const auto native = GetReplayHudRegions(layout, 2560, 1440, {});
        Require(native.mFooter.mTop == 1408 && native.mFooter.mRight == 2392, "Stay inside the native footer");
        Require(native.mSidebar.mLeft == 2392 && native.mSidebar.mTop == 250, "Protect the battlefield and radar");
        const auto scaled = GetReplayHudRegions(layout, 1280, 720, {});
        Require(scaled.mFooter.mTop == 704 && scaled.mSidebar.mLeft == 1196, "Scale both axes");
        const auto viewport = GetReplayHudRegions(layout, 1280, 800, {0, 40, 1280, 760});
        Require(viewport.mFooter.mTop == 744 && viewport.mSidebar.mBottom == 760, "Map the actual letterboxed viewport");
        const auto ambiguous = GetReplayHudRegions(layout, 1280, 800, {});
        Require(ambiguous.mFooter.mTop >= ambiguous.mFooter.mBottom && ambiguous.mSidebar.mTop == 165,
                "Avoid map overlap when letterboxing is ambiguous");
        const auto unknown = GetReplayHudRegions(std::nullopt, 1280, 800, {});
        Require(unknown.mFooter.mBottom == 0 && unknown.mSidebar.mRight == 0, "Unknown layout has no authorized region");
        std::cout << "Replay HUD layout tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
