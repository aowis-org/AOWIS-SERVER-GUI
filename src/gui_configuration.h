#ifndef GUI_CONFIGURATION_H
#define GUI_CONFIGURATION_H

struct GuiConfiguration
{
    bool examples_builtin_enable = true;
};

const GuiConfiguration &guiConfiguration();

#endif // GUI_CONFIGURATION_H
