#include "ColorGradingPresets.h"

const char* ColorGradingPresets::PresetName(Preset p)
{
    switch (p)
    {
        case NEUTRAL:       return "Neutral";
        case HOT_CINEMATIC: return "Hot Cinematic";
        case COLD_BLUEISH:  return "Cold Blueish";
        case DESERT:        return "Desert";
        case NIGHT:         return "Night";
        case GLOOMY:        return "Gloomy";
        case VIBRANT:       return "Vibrant";
        case REALISTIC:     return "Realistic";
        default:            return "Unknown";
    }
}

ColorGradingSettings ColorGradingPresets::GetPreset(Preset p)
{
    ColorGradingSettings s;
    switch (p)
    {
        case NEUTRAL:
            break;
        case HOT_CINEMATIC:
            s.Temperature = 0.45f;
            s.Tint = 0.05f;
            s.Contrast = 1.08f;
            s.Saturation = 0.92f;
            s.BalanceShadows = Vector3(0.08f, -0.02f, -0.06f);
            s.BalanceHighlights = Vector3(-0.04f, -0.02f, 0.0f);
            s.Gain = Vector3(1.03f, 0.97f, 0.92f);
            break;
        case COLD_BLUEISH:
            s.Temperature = -0.5f;
            s.Tint = -0.05f;
            s.Contrast = 1.12f;
            s.Saturation = 0.95f;
            s.BalanceShadows = Vector3(-0.02f, 0.0f, 0.08f);
            s.BalanceHighlights = Vector3(0.0f, 0.01f, 0.08f);
            s.Gain = Vector3(0.95f, 0.98f, 1.05f);
            break;
        case DESERT:
            s.Temperature = 0.6f;
            s.Tint = 0.0f;
            s.Contrast = 1.15f;
            s.Saturation = 1.08f;
            s.Highlights = Vector3(0.9f, 0.7f, 0.4f);
            s.Midtones = Vector3(1.0f, 0.92f, 0.8f);
            s.BalanceHighlights = Vector3(0.1f, -0.05f, -0.12f);
            break;
        case NIGHT:
            s.Temperature = -0.35f;
            s.Saturation = 0.75f;
            s.Brightness = 0.82f;
            s.Contrast = 1.2f;
            s.BalanceShadows = Vector3(-0.02f, -0.02f, 0.1f);
            s.BalanceMidtones = Vector3(0.0f, 0.0f, 0.08f);
            break;
        case GLOOMY:
            s.Contrast = 0.9f;
            s.Saturation = 0.7f;
            s.Brightness = 0.9f;
            s.Temperature = -0.1f;
            s.BalanceShadows = Vector3(0.0f, 0.02f, 0.02f);
            s.BalanceMidtones = Vector3(-0.02f, -0.02f, -0.02f);
            break;
        case VIBRANT:
            s.Saturation = 1.35f;
            s.Contrast = 1.05f;
            s.Temperature = 0.1f;
            s.Gain = Vector3(1.02f, 1.02f, 1.02f);
            break;
        case REALISTIC:
            s.Contrast = 1.02f;
            s.Saturation = 1.02f;
            break;
        default:
            break;
    }
    return s;
}
