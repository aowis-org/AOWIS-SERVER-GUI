#ifndef GUI_CONFIGURATION_H
#define GUI_CONFIGURATION_H

enum class DesktopMapRenderer
{
    Cpu,
    Rhi
};

struct GuiConfiguration
{
    bool examples_builtin_enable = true;
    DesktopMapRenderer map_desktop_renderer = DesktopMapRenderer::Cpu;
};

const GuiConfiguration &guiConfiguration();
DesktopMapRenderer desktopMapRenderer();
bool desktopMapRhiBuildAvailable();
const char *desktopMapRendererName(DesktopMapRenderer renderer);

#endif // GUI_CONFIGURATION_H
