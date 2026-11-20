#pragma once

#include "../../Engine/Renderer/PostProcessing/CommonStructs.h"

struct PostProcessingVolumeComponent {
    bool IsGlobal = false;
    enum Shape { BOX=0, SPHERE=1 } Shape = BOX;
    Vector3 Size = Vector3(20,10,20);
    float Radius = 10.0f; float Falloff = 2.0f;
    int Priority = 0; float BlendWeight = 1.0f;
    uint32_t OverrideMask = OVERRIDE_ALL;
    GlobalLightingSettings    Global;
    ColorGradingSettings      Color;
    ToneMappingSettings       Tone;
    LUTSettings               Lut;
    BloomSettings             Bloom;
    VignetteSettings          Vignette;
    ChromaticAberrationSettings Chromatic;
    SharpenSettings           Sharpen;
    LensDistortionSettings    LensDistortion;
    FilmGrainSettings         FilmGrain;
    DoFSettings               DoF;
    MotionBlurSettings        MotionBlur;
};
