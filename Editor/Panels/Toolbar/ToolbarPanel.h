#pragma once

class Application;

class ToolbarPanel
{
public:
    static void Draw(Application& app);
    static void DrawContents(Application& app);

    static bool IsRecording() { return s_IsRecording; }
    static void SetRecording(bool recording) { s_IsRecording = recording; }

private:
    static bool s_IsRecording;
};
