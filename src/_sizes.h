#ifndef SIZES_H
#define SIZES_H

namespace Sizes
{
// Q_OS_WASM
#ifdef __EMSCRIPTEN__
    inline constexpr int SidebarLeftWidth = 240;
    inline constexpr int SidebarMapEditLeftWidth = 240;
#else
    inline constexpr int SidebarLeftWidth = 210;
    inline constexpr int SidebarMapEditLeftWidth = 230;
#endif
    inline constexpr int SidebarRightWidth = 210;
    inline constexpr int SidebarRightImageHeight = 100;
}


#endif
