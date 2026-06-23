#ifndef SIZES_H
#define SIZES_H

namespace Sizes
{
// Q_OS_WASM
#ifdef __EMSCRIPTEN__
    inline constexpr int SidebarLeftWidthBase = 240;
    inline constexpr int SidebarMapEditLeftWidthBase = 220;
#else
    inline constexpr int SidebarLeftWidthBase = 210;
    inline constexpr int SidebarMapEditLeftWidthBase = 230;
#endif
}


#endif
