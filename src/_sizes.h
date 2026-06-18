#ifndef SIZES_H
#define SIZES_H

namespace Sizes
{
// Q_OS_WASM
#ifdef __EMSCRIPTEN__
    inline constexpr int SidebarLeftWidthBase = 240;
#else
    inline constexpr int SidebarLeftWidthBase = 210;
#endif
}


#endif
