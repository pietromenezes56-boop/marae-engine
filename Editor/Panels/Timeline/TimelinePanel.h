#pragma once

class Application;

class TimelinePanel
{
public:
    static void Draw(Application& app);

    static float GetPlaybackTime() { return s_PlaybackTime; }
    static void SetPlaybackTime(float time) { s_PlaybackTime = time; }
    static void AdvanceRecording(float deltaTime);

private:
    static float s_PlaybackTime;
    static bool s_IsPlaying;
    static float s_TimeScale;

    static bool s_ShowPosX;
    static bool s_ShowPosY;
    static bool s_ShowPosZ;
    static bool s_ShowRotX;
    static bool s_ShowRotY;
    static bool s_ShowRotZ;
    static bool s_ShowSclX;
    static bool s_ShowSclY;
    static bool s_ShowSclZ;
    static bool s_PositionExpanded;
    static bool s_RotationExpanded;
    static bool s_ScaleExpanded;

    static int s_SelectedKeyType;
    static int s_SelectedKeyIndex;
};
