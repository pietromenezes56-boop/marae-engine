using System;
using System.Runtime.InteropServices;

namespace MaraeEngine.Internal
{
    /// <summary>
    /// ContÃ©m todos os delegates gerenciados que mapeiam para ponteiros de funÃ§Ã£o nativos (C++),
    /// recebidos no bootstrap. Os tipos seguem a convenÃ§Ã£o Cdecl para interoperabilidade com a engine nativa.
    /// </summary>
    internal static class NativeDelegates
    {
        #region Logging

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void LogDelegate(int level, [MarshalAs(UnmanagedType.LPWStr)] string message);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void EntityApplyImpulseDelegate(ulong entityId, float x, float y, float z);
        internal static EntityApplyImpulseDelegate? EntityApplyImpulse;
        internal static LogDelegate? Log;

        #endregion

        #region Entity Queries

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int EntityCountDelegate();
        internal static EntityCountDelegate? EntityCount;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityGetIDDelegate(ulong entityHandle);
        internal static EntityGetIDDelegate? EntityGetID;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate IntPtr EntityGetNameDelegate(ulong entityHandle);
        internal static EntityGetNameDelegate? EntityGetName;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void EntitySetNameDelegate(ulong entityHandle, [MarshalAs(UnmanagedType.LPWStr)] string name);
        internal static EntitySetNameDelegate? EntitySetName;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool EntityGetActiveDelegate(ulong entityHandle);
        internal static EntityGetActiveDelegate? EntityGetActive;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void EntitySetActiveDelegate(ulong entityHandle, [MarshalAs(UnmanagedType.I1)] bool active);
        internal static EntitySetActiveDelegate? EntitySetActive;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate IntPtr EntityGetTagDelegate(ulong entityHandle);
        internal static EntityGetTagDelegate? EntityGetTag;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void EntitySetTagDelegate(ulong entityHandle, [MarshalAs(UnmanagedType.LPWStr)] string tag);
        internal static EntitySetTagDelegate? EntitySetTag;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int EntityGetLayerDelegate(ulong entityHandle);
        internal static EntityGetLayerDelegate? EntityGetLayer;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void EntitySetLayerDelegate(ulong entityHandle, int layer);
        internal static EntitySetLayerDelegate? EntitySetLayer;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityGetTransformDelegate(ulong entityHandle);
        internal static EntityGetTransformDelegate? EntityGetTransform;

        #endregion

        #region Transform (Position / Scale / Rotation)

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformGetVec3OutDelegate(ulong transformHandle, out float x, out float y, out float z);
        internal static TransformGetVec3OutDelegate? TransformGetLocalPosition;
        internal static TransformGetVec3OutDelegate? TransformGetWorldPosition;
        internal static TransformGetVec3OutDelegate? TransformGetLocalScale;
        internal static TransformGetVec3OutDelegate? TransformGetEuler;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformSetVec3Delegate(ulong transformHandle, float x, float y, float z);
        internal static TransformSetVec3Delegate? TransformSetLocalPosition;
        internal static TransformSetVec3Delegate? TransformSetWorldPosition;
        internal static TransformSetVec3Delegate? TransformSetLocalScale;
        internal static TransformSetVec3Delegate? TransformSetEuler;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformGetQuatOutDelegate(ulong transformHandle, out float x, out float y, out float z, out float w);
        internal static TransformGetQuatOutDelegate? TransformGetLocalRotation;
        internal static TransformGetQuatOutDelegate? TransformGetWorldRotation;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformSetQuatDelegate(ulong transformHandle, float x, float y, float z, float w);
        internal static TransformSetQuatDelegate? TransformSetLocalRotation;
        internal static TransformSetQuatDelegate? TransformSetWorldRotation;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformTranslateDeltaDelegate(ulong transformHandle, float dx, float dy, float dz, int space);
        internal static TransformTranslateDeltaDelegate? TransformTranslateDelta;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformRotateEulerDelegate(ulong transformHandle, float ex, float ey, float ez, int space);
        internal static TransformRotateEulerDelegate? TransformRotateEuler;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformLookAtDelegate(ulong transformHandle, float tx, float ty, float tz);
        internal static TransformLookAtDelegate? TransformLookAt;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TransformSetParentDelegate(ulong transformHandle, ulong parentEntityId, [MarshalAs(UnmanagedType.I1)] bool worldPositionStays);
        internal static TransformSetParentDelegate? TransformSetParent;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int TransformGetChildCountDelegate(ulong transformHandle);
        internal static TransformGetChildCountDelegate? TransformGetChildCount;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong TransformGetChildDelegate(ulong transformHandle, int index);
        internal static TransformGetChildDelegate? TransformGetChild;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong TransformGetParentDelegate(ulong transformHandle);
        internal static TransformGetParentDelegate? TransformGetParent;

        #endregion

        #region Entity Find / Destroy / Instantiate

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate ulong EntityFindByNameDelegate([MarshalAs(UnmanagedType.LPWStr)] string name);
        internal static EntityFindByNameDelegate? EntityFindByName;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate ulong EntityFindByTagDelegate([MarshalAs(UnmanagedType.LPWStr)] string tag);
        internal static EntityFindByTagDelegate? EntityFindByTag;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityFindByIdDelegate(ulong id);
        internal static EntityFindByIdDelegate? EntityFindById;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate IntPtr EntityFindAllByTagDelegate([MarshalAs(UnmanagedType.LPWStr)] string tag, out int count);
        internal static EntityFindAllByTagDelegate? EntityFindAllByTag;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void EntityDestroyDelegate(ulong entityHandle);
        internal static EntityDestroyDelegate? EntityDestroy;
        internal static EntityDestroyDelegate? EntityDestroyHierarchy;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityInstantiateDelegate(ulong sourceEntityHandle);
        internal static EntityInstantiateDelegate? EntityInstantiate;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool EntityHasUIDelegate(ulong entityHandle);
        internal static EntityHasUIDelegate? EntityHasUI;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int UIGetTypeDelegate(ulong entityHandle);
        internal static UIGetTypeDelegate? UIGetType;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool UIGetEnabledDelegate(ulong entityHandle);
        internal static UIGetEnabledDelegate? UIGetEnabled;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void UISetEnabledDelegate(ulong entityHandle, [MarshalAs(UnmanagedType.I1)] bool enabled);
        internal static UISetEnabledDelegate? UISetEnabled;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void UIGetRectDelegate(ulong entityHandle, out float x, out float y, out float width, out float height);
        internal static UIGetRectDelegate? UIGetRect;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool UIIsPointerOverDelegate(ulong entityHandle);
        internal static UIIsPointerOverDelegate? UIIsPointerOver;

        #endregion

        #region Input

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetKeyDelegate(int keyCode);
        internal static InputGetKeyDelegate? InputGetKey;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetKeyDownDelegate(int keyCode);
        internal static InputGetKeyDownDelegate? InputGetKeyDown;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetKeyUpDelegate(int keyCode);
        internal static InputGetKeyUpDelegate? InputGetKeyUp;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetMouseButtonDelegate(int button);
        internal static InputGetMouseButtonDelegate? InputGetMouseButton;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetMouseButtonDownDelegate(int button);
        internal static InputGetMouseButtonDownDelegate? InputGetMouseButtonDown;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetMouseButtonUpDelegate(int button);
        internal static InputGetMouseButtonUpDelegate? InputGetMouseButtonUp;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void InputGetMousePosDelegate(out float x, out float y, out float z);
        internal static InputGetMousePosDelegate? InputGetMousePos;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate float InputGetAxisDelegate([MarshalAs(UnmanagedType.LPWStr)] string axisName);
        internal static InputGetAxisDelegate? InputGetAxis;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetActionDelegate([MarshalAs(UnmanagedType.LPWStr)] string actionName);
        internal static InputGetActionDelegate? InputGetAction;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetActionDownDelegate([MarshalAs(UnmanagedType.LPWStr)] string actionName);
        internal static InputGetActionDownDelegate? InputGetActionDown;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool InputGetActionUpDelegate([MarshalAs(UnmanagedType.LPWStr)] string actionName);
        internal static InputGetActionUpDelegate? InputGetActionUp;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void InputBindKeyDelegate([MarshalAs(UnmanagedType.LPWStr)] string actionName, int keyCode, float scale);
        internal static InputBindKeyDelegate? InputBindKey;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void InputBindMouseButtonDelegate([MarshalAs(UnmanagedType.LPWStr)] string actionName, int button, float scale);
        internal static InputBindMouseButtonDelegate? InputBindMouseButton;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void InputClearBindingsDelegate([MarshalAs(UnmanagedType.LPWStr)] string actionName);
        internal static InputClearBindingsDelegate? InputClearBindings;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void InputSetDeadZoneDelegate([MarshalAs(UnmanagedType.LPWStr)] string axisName, float deadZone);
        internal static InputSetDeadZoneDelegate? InputSetDeadZone;

        #endregion

        #region Time

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float TimeGetDeltaDelegate();
        internal static TimeGetDeltaDelegate? TimeGetDelta;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float TimeGetUnscaledDeltaDelegate();
        internal static TimeGetUnscaledDeltaDelegate? TimeGetUnscaledDelta;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float TimeGetTimeDelegate();
        internal static TimeGetTimeDelegate? TimeGetTime;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float TimeGetFixedDeltaDelegate();
        internal static TimeGetFixedDeltaDelegate? TimeGetFixedDelta;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float TimeGetTimeScaleDelegate();
        internal static TimeGetTimeScaleDelegate? TimeGetTimeScale;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void TimeSetTimeScaleDelegate(float scale);
        internal static TimeSetTimeScaleDelegate? TimeSetTimeScale;

        #endregion

        #region Components - Generic

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool EntityHasComponentDelegate(ulong entityHandle, int compType);
        internal static EntityHasComponentDelegate? EntityHasComponent;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate ulong EntityGetComponentDelegate(ulong entityHandle, int compType);
        internal static EntityGetComponentDelegate? EntityGetComponent;

        #endregion

        #region Component - Mesh

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate IntPtr ComponentMeshGetNameDelegate(ulong handle);
        internal static ComponentMeshGetNameDelegate? ComponentMeshGetName;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void ComponentMeshSetNameDelegate(ulong handle, [MarshalAs(UnmanagedType.LPWStr)] string name);
        internal static ComponentMeshSetNameDelegate? ComponentMeshSetName;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool ComponentMeshGetVisibleDelegate(ulong handle);
        internal static ComponentMeshGetVisibleDelegate? ComponentMeshGetVisible;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMeshSetVisibleDelegate(ulong handle, [MarshalAs(UnmanagedType.I1)] bool visible);
        internal static ComponentMeshSetVisibleDelegate? ComponentMeshSetVisible;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMeshGetColorDelegate(ulong handle, out float r, out float g, out float b, out float a);
        internal static ComponentMeshGetColorDelegate? ComponentMeshGetColor;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMeshSetColorDelegate(ulong handle, float r, float g, float b, float a);
        internal static ComponentMeshSetColorDelegate? ComponentMeshSetColor;

        #endregion

        #region Component - Audio

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentAudioPlayDelegate(ulong handle);
        internal static ComponentAudioPlayDelegate? ComponentAudioPlay;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentAudioStopDelegate(ulong handle);
        internal static ComponentAudioStopDelegate? ComponentAudioStop;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool ComponentAudioGetPlayingDelegate(ulong handle);
        internal static ComponentAudioGetPlayingDelegate? ComponentAudioGetPlaying;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentAudioGetVolumeDelegate(ulong handle);
        internal static ComponentAudioGetVolumeDelegate? ComponentAudioGetVolume;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentAudioSetVolumeDelegate(ulong handle, float v);
        internal static ComponentAudioSetVolumeDelegate? ComponentAudioSetVolume;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool ComponentAudioGetLoopDelegate(ulong handle);
        internal static ComponentAudioGetLoopDelegate? ComponentAudioGetLoop;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentAudioSetLoopDelegate(ulong handle, [MarshalAs(UnmanagedType.I1)] bool loop);
        internal static ComponentAudioSetLoopDelegate? ComponentAudioSetLoop;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentAudioGetPitchDelegate(ulong handle);
        internal static ComponentAudioGetPitchDelegate? ComponentAudioGetPitch;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentAudioSetPitchDelegate(ulong handle, float pitch);
        internal static ComponentAudioSetPitchDelegate? ComponentAudioSetPitch;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool AudioPlayBySourceIDDelegate(
            [MarshalAs(UnmanagedType.LPWStr)] string sourceID);
        internal static AudioPlayBySourceIDDelegate? AudioPlayBySourceID;

        #endregion

        #region Component - Light

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ComponentLightGetTypeDelegate(ulong handle);
        internal static ComponentLightGetTypeDelegate? ComponentLightGetType;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentLightSetTypeDelegate(ulong handle, int type);
        internal static ComponentLightSetTypeDelegate? ComponentLightSetType;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentLightGetIntensityDelegate(ulong handle);
        internal static ComponentLightGetIntensityDelegate? ComponentLightGetIntensity;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentLightSetIntensityDelegate(ulong handle, float v);
        internal static ComponentLightSetIntensityDelegate? ComponentLightSetIntensity;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentLightGetColorDelegate(ulong handle, out float r, out float g, out float b, out float a);
        internal static ComponentLightGetColorDelegate? ComponentLightGetColor;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentLightSetColorDelegate(ulong handle, float r, float g, float b, float a);
        internal static ComponentLightSetColorDelegate? ComponentLightSetColor;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentLightGetRangeDelegate(ulong handle);
        internal static ComponentLightGetRangeDelegate? ComponentLightGetRange;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentLightSetRangeDelegate(ulong handle, float range);
        internal static ComponentLightSetRangeDelegate? ComponentLightSetRange;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentLightGetSpotAngleDelegate(ulong handle);
        internal static ComponentLightGetSpotAngleDelegate? ComponentLightGetSpotAngle;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentLightSetSpotAngleDelegate(ulong handle, float spotAngle);
        internal static ComponentLightSetSpotAngleDelegate? ComponentLightSetSpotAngle;

        #endregion

        #region Component - Material

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMaterialGetAlbedoDelegate(ulong handle, out float r, out float g, out float b, out float a);
        internal static ComponentMaterialGetAlbedoDelegate? ComponentMaterialGetAlbedo;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMaterialSetAlbedoDelegate(ulong handle, float r, float g, float b, float a);
        internal static ComponentMaterialSetAlbedoDelegate? ComponentMaterialSetAlbedo;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentMaterialGetMetallicDelegate(ulong handle);
        internal static ComponentMaterialGetMetallicDelegate? ComponentMaterialGetMetallic;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMaterialSetMetallicDelegate(ulong handle, float metallic);
        internal static ComponentMaterialSetMetallicDelegate? ComponentMaterialSetMetallic;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentMaterialGetSmoothnessDelegate(ulong handle);
        internal static ComponentMaterialGetSmoothnessDelegate? ComponentMaterialGetSmoothness;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentMaterialSetSmoothnessDelegate(ulong handle, float smoothness);
        internal static ComponentMaterialSetSmoothnessDelegate? ComponentMaterialSetSmoothness;

        #endregion

        #region Component - Script

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool ComponentScriptGetEnabledDelegate(ulong handle);
        internal static ComponentScriptGetEnabledDelegate? ComponentScriptGetEnabled;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentScriptSetEnabledDelegate(ulong handle, [MarshalAs(UnmanagedType.I1)] bool enabled);
        internal static ComponentScriptSetEnabledDelegate? ComponentScriptSetEnabled;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate IntPtr ComponentScriptGetTypeNameDelegate(ulong handle);
        internal static ComponentScriptGetTypeNameDelegate? ComponentScriptGetTypeName;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate IntPtr ComponentScriptGetFieldsJsonDelegate(ulong handle);
        internal static ComponentScriptGetFieldsJsonDelegate? ComponentScriptGetFieldsJson;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void ComponentScriptSetFieldsJsonDelegate(ulong handle, [MarshalAs(UnmanagedType.LPWStr)] string json);
        internal static ComponentScriptSetFieldsJsonDelegate? ComponentScriptSetFieldsJson;

        #endregion

        #region Generic Component Property Bridge

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool ComponentGetBoolDelegate(ulong entityId, int componentType, int property);
        internal static ComponentGetBoolDelegate? ComponentGetBool;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentSetBoolDelegate(ulong entityId, int componentType, int property,
            [MarshalAs(UnmanagedType.I1)] bool value);
        internal static ComponentSetBoolDelegate? ComponentSetBool;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate int ComponentGetIntDelegate(ulong entityId, int componentType, int property);
        internal static ComponentGetIntDelegate? ComponentGetInt;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentSetIntDelegate(ulong entityId, int componentType, int property, int value);
        internal static ComponentSetIntDelegate? ComponentSetInt;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate float ComponentGetFloatDelegate(ulong entityId, int componentType, int property);
        internal static ComponentGetFloatDelegate? ComponentGetFloat;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentSetFloatDelegate(ulong entityId, int componentType, int property, float value);
        internal static ComponentSetFloatDelegate? ComponentSetFloat;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentGetVector4Delegate(ulong entityId, int componentType, int property,
            out float x, out float y, out float z, out float w);
        internal static ComponentGetVector4Delegate? ComponentGetVector4;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentSetVector4Delegate(ulong entityId, int componentType, int property,
            float x, float y, float z, float w);
        internal static ComponentSetVector4Delegate? ComponentSetVector4;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate IntPtr ComponentGetStringDelegate(ulong entityId, int componentType, int property);
        internal static ComponentGetStringDelegate? ComponentGetString;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void ComponentSetStringDelegate(ulong entityId, int componentType, int property,
            [MarshalAs(UnmanagedType.LPWStr)] string value);
        internal static ComponentSetStringDelegate? ComponentSetString;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void ComponentInvokeDelegate(ulong entityId, int componentType, int action);
        internal static ComponentInvokeDelegate? ComponentInvoke;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate IntPtr NavigationFindPathDelegate(float sx, float sy, float sz, float dx, float dy, float dz);
        internal static NavigationFindPathDelegate? NavigationFindPath;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void AnimationSetBoolDelegate(ulong entityId,
            [MarshalAs(UnmanagedType.LPWStr)] string parameter,
            [MarshalAs(UnmanagedType.I1)] bool value);
        internal static AnimationSetBoolDelegate? AnimationSetBool;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal delegate bool ScriptDebugShouldBreakDelegate(ulong entityId,
            [MarshalAs(UnmanagedType.LPWStr)] string typeName,
            [MarshalAs(UnmanagedType.LPWStr)] string methodName);
        internal static ScriptDebugShouldBreakDelegate? ScriptDebugShouldBreak;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void ScriptDebugReportExceptionDelegate(ulong entityId,
            [MarshalAs(UnmanagedType.LPWStr)] string typeName,
            [MarshalAs(UnmanagedType.LPWStr)] string methodName,
            [MarshalAs(UnmanagedType.LPWStr)] string message,
            [MarshalAs(UnmanagedType.LPWStr)] string stackTrace,
            [MarshalAs(UnmanagedType.LPWStr)] string variablesJson);
        internal static ScriptDebugReportExceptionDelegate? ScriptDebugReportException;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        internal delegate void ScriptDebugReportBreakpointDelegate(ulong entityId,
            [MarshalAs(UnmanagedType.LPWStr)] string typeName,
            [MarshalAs(UnmanagedType.LPWStr)] string methodName,
            [MarshalAs(UnmanagedType.LPWStr)] string variablesJson);
        internal static ScriptDebugReportBreakpointDelegate? ScriptDebugReportBreakpoint;

        #endregion
    }
}

