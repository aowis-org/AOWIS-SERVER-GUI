#ifndef SIZES_H
#define SIZES_H

namespace Sizes
{
#ifdef __EMSCRIPTEN__
    inline constexpr int SidebarLeftWidthBase = 240;
#else
    inline constexpr int SidebarLeftWidthBase = 210;
#endif
}


#endif
