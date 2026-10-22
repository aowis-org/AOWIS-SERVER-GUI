#include "tabs/map_workspace_mode.h"

#include <cstdio>
#include <cstring>

namespace
{
bool expect(bool valid, const char *description)
{
    if (!valid)
        std::fprintf(stderr, "map_workspace_contract: %s\n", description);
    return valid;
}
}

int main()
{
    using namespace MapWorkspace;

    if (!expect(modeInfo(-1) == nullptr && modeInfo(4) == nullptr,
                "invalid tab indices must be rejected"))
        return 1;

    const char *expected_titles[] = {
        "Distribution Monitor", "Distribution Editor",
        "Drainage Monitor", "Drainage Editor"
    };
    const Domain expected_domains[] = {
        Domain::Distribution, Domain::Distribution,
        Domain::Drainage, Domain::Drainage
    };
    const Activity expected_activities[] = {
        Activity::Monitor, Activity::Editor,
        Activity::Monitor, Activity::Editor
    };
    for (int index = 0; index < static_cast<int>(Modes.size()); ++index)
    {
        const ModeInfo *info = modeInfo(index);
        if (!expect(info != nullptr, "missing mode")
            || !expect(std::strcmp(info->title, expected_titles[index]) == 0,
                       "mode label/order changed")
            || !expect(info->domain == expected_domains[index], "incorrect map domain")
            || !expect(info->activity == expected_activities[index], "incorrect map activity")
            || !expect(info->icon_resource[0] == ':', "missing Qt icon resource")
            || !expect(isImplemented(index) == (index < 2),
                       "drainage modes must remain blank until implemented"))
            return 1;
    }
    if (!expect(!isImplemented(-1) && !isImplemented(4),
                "invalid modes cannot own tools"))
        return 1;

    std::puts("Map workspace mode/ownership contract passed");
    return 0;
}
