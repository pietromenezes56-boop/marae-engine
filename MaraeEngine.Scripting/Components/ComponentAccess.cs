using System;
using System.Runtime.InteropServices;
using MaraeEngine.Internal;

namespace MaraeEngine
{
    // Stable IDs shared with ComponentType in Engine/Scripting/ScriptingAPI.h.
    internal static class ComponentIds
    {
        internal const int Mesh = 1, Audio = 2, Light = 3, Material = 4, Script = 5;
        internal const int Physics = 6, UI = 7, Animation = 8, Camera = 9, GlobalLight = 10;
    }

    // Property IDs are deliberately scoped by component type. Additions do not
    // change the native ABI because all values travel through one typed bridge.
    internal static class ComponentAccess
    {
        internal static bool GetBool(ulong id, int type, int property) =>
            NativeDelegates.ComponentGetBool?.Invoke(id, type, property) ?? false;
        internal static void SetBool(ulong id, int type, int property, bool value) =>
            NativeDelegates.ComponentSetBool?.Invoke(id, type, property, value);
        internal static int GetInt(ulong id, int type, int property) =>
            NativeDelegates.ComponentGetInt?.Invoke(id, type, property) ?? 0;
        internal static void SetInt(ulong id, int type, int property, int value) =>
            NativeDelegates.ComponentSetInt?.Invoke(id, type, property, value);
        internal static float GetFloat(ulong id, int type, int property) =>
            NativeDelegates.ComponentGetFloat?.Invoke(id, type, property) ?? 0f;
        internal static void SetFloat(ulong id, int type, int property, float value) =>
            NativeDelegates.ComponentSetFloat?.Invoke(id, type, property, value);

        internal static Color GetColor(ulong id, int type, int property)
        {
            float r = 1f, g = 1f, b = 1f, a = 1f;
            NativeDelegates.ComponentGetVector4?.Invoke(id, type, property, out r, out g, out b, out a);
            return new Color(r, g, b, a);
        }

        internal static void SetColor(ulong id, int type, int property, Color value) =>
            NativeDelegates.ComponentSetVector4?.Invoke(id, type, property,
                value.R, value.G, value.B, value.A);

        internal static Vector3 GetVector3(ulong id, int type, int property)
        {
            float x = 0f, y = 0f, z = 0f, w = 0f;
            NativeDelegates.ComponentGetVector4?.Invoke(id, type, property, out x, out y, out z, out w);
            return new Vector3(x, y, z);
        }

        internal static void SetVector3(ulong id, int type, int property, Vector3 value) =>
            NativeDelegates.ComponentSetVector4?.Invoke(id, type, property,
                value.X, value.Y, value.Z, 0f);

        internal static string GetString(ulong id, int type, int property)
        {
            IntPtr value = NativeDelegates.ComponentGetString?.Invoke(id, type, property) ?? IntPtr.Zero;
            return value == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUni(value) ?? string.Empty;
        }

        internal static void SetString(ulong id, int type, int property, string value) =>
            NativeDelegates.ComponentSetString?.Invoke(id, type, property, value ?? string.Empty);

        internal static void Invoke(ulong id, int type, int action) =>
            NativeDelegates.ComponentInvoke?.Invoke(id, type, action);
    }

    internal static class PhysicsProperty
    {
        internal const int Enabled = 0, UseGravity = 1, IsStatic = 2, ColliderEnabled = 3;
        internal const int ContinuousCollision = 4, FreezeRotation = 5, IsTrigger = 6;
        internal const int ColliderShape = 0;
        internal const int Mass = 0, Restitution = 1, Friction = 2;
        internal const int Velocity = 0, AngularVelocity = 1;
        internal const int ApplyImpulse = 0;
    }

    internal static class UIProperty
    {
        internal const int Enabled = 0, ClipsContent = 1, UseResponsiveAnchors = 2;
        internal const int Checked = 3, DropdownOpen = 4, NineSlice = 5, ScrollViewEnabled = 6;
        // Estado de runtime (atualizado pelo sistema de interação da UI).
        internal const int IsHovered = 7, IsPressed = 8, IsFocused = 9;
        internal const int Type = 0, ZIndex = 1, TextAlignment = 2;
        internal const int SelectedIndex = 3, HoverOptionIndex = 4, CaretIndex = 5;
        internal const int X = 0, Y = 1, Width = 2, Height = 3;
        internal const int AnchorX = 4, AnchorY = 5, Rotation = 6, Transparency = 7;
        internal const int CornerRadius = 8, BorderThickness = 9, FontSize = 10;
        internal const int AnchorMinX = 11, AnchorMinY = 12, AnchorMaxX = 13, AnchorMaxY = 14;
        internal const int OffsetMinX = 15, OffsetMinY = 16, OffsetMaxX = 17, OffsetMaxY = 18;
        internal const int Value = 19, MinValue = 20, MaxValue = 21, ScrollOffsetY = 22;
        internal const int SliceLeft = 23, SliceTop = 24, SliceRight = 25, SliceBottom = 26;
        internal const int BackgroundColor = 0, TextColor = 1, BorderColor = 2;
        internal const int HoverColor = 3, PressedColor = 4, FillColor = 5, TrackColor = 6;
        internal const int Text = 0, Font = 1, ImagePath = 2, Options = 3;
    }

    internal static class LightProperty
    {
        internal const int Enabled = 0, CastShadows = 1, UseTemperature = 2;
        internal const int DistanceSoftening = 3, IndirectContribution = 4;
        internal const int Type = 0;
        internal const int Intensity = 0, Range = 1, SpotAngle = 2, SourceRadius = 3;
        internal const int ShadowSoftness = 4, ShadowStrength = 5, ContactShadowStrength = 6;
        internal const int TemperatureKelvin = 7, Color = 0;
    }

    internal static class AnimationProperty
    {
        internal const int Playing = 0, Enabled = 1, PlayOnStart = 2;
        internal const int Time = 0, Speed = 1, Weight = 2;
        internal const int Play = 0, Pause = 1, Stop = 2;
    }

    internal static class CameraProperty
    {
        internal const int Enabled = 0, PreviewEnabled = 1, Orthographic = 2;
        internal const int FieldOfView = 0, NearClip = 1, FarClip = 2, OrthographicSize = 3;
    }

    internal static class GlobalLightProperty
    {
        internal const int Enabled = 0, FogEnabled = 1, CloudsEnabled = 2, CloudMovementEnabled = 3;
        internal const int AmbientIntensity = 0, FogDensity = 1, FogStart = 2, FogEnd = 3;
        internal const int ShadowStrength = 4, Exposure = 5, CloudAmount = 6, CloudSpeed = 7;
        internal const int AmbientColor = 0, FogColor = 1;
    }
}
