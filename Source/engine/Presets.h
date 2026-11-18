// Factory presets. The first one loads when the plugin opens.
#pragma once

#include <vector>
#include "Engine.h"

namespace eb
{
struct Preset
{
    const char* name;
    const char* category;
    Params p;
};

const std::vector<Preset>& factoryPresets();
} // namespace eb
