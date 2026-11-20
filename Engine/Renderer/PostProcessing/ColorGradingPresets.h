#pragma once

#include "CommonStructs.h"

struct ColorGradingPresets {
    enum Preset { NEUTRAL=0, HOT_CINEMATIC, COLD_BLUEISH, DESERT, NIGHT, GLOOMY, VIBRANT, REALISTIC, COUNT };
    static const char* PresetName(Preset p);
    static ColorGradingSettings GetPreset(Preset p);
};
