using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace MaraeEngine.Internal
{
    /// <summary>
    /// Classe estática de inicialização: copia ponteiros de função nativos (struct NativeAPI do C++)
    /// para <see cref="NativeDelegates"/> e retorna ponteiro para <c>ManagedAPI</c> com os
    /// callbacks usados pelo runtime C++ (OnStart, OnUpdate, etc.).
    /// </summary>
    public static class Bootstrap
    {
        private static bool _initialized;

        #region Managed Delegates (declarados no escopo da classe para permitir attributes)

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate IntPtr CreateScriptInstanceDlg(ulong entityHandle, [MarshalAs(UnmanagedType.LPWStr)] string typeName);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void DestroyScriptInstanceDlg(IntPtr instance);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CallOneDlg(IntPtr instance);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CallUpdateDlg(IntPtr instance, float dt);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GetFieldsDlg(IntPtr instance);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int SetFieldDlg(IntPtr instance, [MarshalAs(UnmanagedType.LPWStr)] string fieldName, [MarshalAs(UnmanagedType.LPWStr)] string jsonValue);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int ReloadScriptsDlg([MarshalAs(UnmanagedType.LPWStr)] string scriptsFolder, [MarshalAs(UnmanagedType.LPWStr)] string outputFolder);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int CompileScriptsFolderDlg([MarshalAs(UnmanagedType.LPWStr)] string scriptsFolder, [MarshalAs(UnmanagedType.LPWStr)] string outputFolder,
            out IntPtr errors, out int errorCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void FreeManagedBufferDlg(IntPtr buffer);

        // Diagnóstico do profiler: out int/long são marshaled como ponteiros
        // no ABI não-gerenciado, compatível com int*/long long* do C++.
        private delegate int GetGCStatsDlg(out int gen0, out int gen1, out int gen2, out long heapBytes);
        private static readonly FreeManagedBufferDlg FreeManagedBufferCallback =
            buffer => { if (buffer != IntPtr.Zero) Marshal.FreeHGlobal(buffer); };

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CallCollisionDlg(IntPtr instance, ulong otherEntity,
            float cx, float cy, float cz, float nx, float ny, float nz, float impulse);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CallTriggerDlg(IntPtr instance, ulong otherEntity);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CallUIDlg(IntPtr instance, ulong entityId);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CallUIValueDlg(IntPtr instance, ulong entityId, float value);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int CallUITextDlg(IntPtr instance, ulong entityId, [MarshalAs(UnmanagedType.LPWStr)] string text);

        #endregion

        /// <summary>
        /// Estrutura não-gerenciada que aponta para todas as funções nativas a serem chamadas
        /// a partir do lado C#. Cada campo é um ponteiro de função do C++ e sua ordem DEVE
        /// corresponder exatamente ao struct NativeAPI declarado no lado C++ (ScriptingRuntime.h).
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        private unsafe struct NativeApiTable
        {
            public IntPtr Log;

            public IntPtr EntityCount;
            public IntPtr EntityGetID;
            public IntPtr EntityGetName;
            public IntPtr EntitySetName;
            public IntPtr EntityApplyImpulse;
            public IntPtr EntityGetActive;
            public IntPtr EntitySetActive;
            public IntPtr EntityGetTag;
            public IntPtr EntitySetTag;
            public IntPtr EntityGetLayer;
            public IntPtr EntitySetLayer;
            public IntPtr EntityGetTransform;

            public IntPtr TransformGetLocalPosition;
            public IntPtr TransformSetLocalPosition;
            public IntPtr TransformGetWorldPosition;
            public IntPtr TransformSetWorldPosition;
            public IntPtr TransformGetLocalRotation;
            public IntPtr TransformSetLocalRotation;
            public IntPtr TransformGetWorldRotation;
            public IntPtr TransformSetWorldRotation;
            public IntPtr TransformGetLocalScale;
            public IntPtr TransformSetLocalScale;
            public IntPtr TransformGetEuler;
            public IntPtr TransformSetEuler;
            public IntPtr TransformTranslateDelta;
            public IntPtr TransformRotateEuler;
            public IntPtr TransformLookAt;
            public IntPtr TransformSetParent;
            public IntPtr TransformGetChildCount;
            public IntPtr TransformGetChild;
            public IntPtr TransformGetParent;

            public IntPtr EntityFindByName;
            public IntPtr EntityFindByTag;
            public IntPtr EntityFindById;
            public IntPtr EntityFindAllByTag;
            public IntPtr EntityDestroy;
            public IntPtr EntityDestroyHierarchy;
            public IntPtr EntityInstantiate;

            public IntPtr EntityHasUI;
            public IntPtr UIGetType;
            public IntPtr UIGetEnabled;
            public IntPtr UISetEnabled;
            public IntPtr UIGetRect;
            public IntPtr UIIsPointerOver;

            public IntPtr InputGetKey;
            public IntPtr InputGetKeyDown;
            public IntPtr InputGetKeyUp;
            public IntPtr InputGetMouseButton;
            public IntPtr InputGetMouseButtonDown;
            public IntPtr InputGetMouseButtonUp;
            public IntPtr InputGetMousePos;
            public IntPtr InputGetAxis;
            public IntPtr InputGetAction;
            public IntPtr InputGetActionDown;
            public IntPtr InputGetActionUp;
            public IntPtr InputBindKey;
            public IntPtr InputBindMouseButton;
            public IntPtr InputClearBindings;
            public IntPtr InputSetDeadZone;

            public IntPtr TimeGetDelta;
            public IntPtr TimeGetUnscaledDelta;
            public IntPtr TimeGetTime;
            public IntPtr TimeGetFixedDelta;
            public IntPtr TimeGetTimeScale;
            public IntPtr TimeSetTimeScale;

            public IntPtr EntityHasComponent;
            public IntPtr EntityGetComponent;

            public IntPtr ComponentMeshGetName;
            public IntPtr ComponentMeshSetName;
            public IntPtr ComponentMeshGetVisible;
            public IntPtr ComponentMeshSetVisible;
            public IntPtr ComponentMeshGetColor;
            public IntPtr ComponentMeshSetColor;

            public IntPtr ComponentAudioPlay;
            public IntPtr ComponentAudioStop;
            public IntPtr ComponentAudioGetPlaying;
            public IntPtr ComponentAudioGetVolume;
            public IntPtr ComponentAudioSetVolume;
            public IntPtr ComponentAudioGetLoop;
            public IntPtr ComponentAudioSetLoop;
            public IntPtr ComponentAudioGetPitch;
            public IntPtr ComponentAudioSetPitch;
            public IntPtr AudioPlayBySourceID;

            public IntPtr ComponentLightGetType;
            public IntPtr ComponentLightSetType;
            public IntPtr ComponentLightGetIntensity;
            public IntPtr ComponentLightSetIntensity;
            public IntPtr ComponentLightGetColor;
            public IntPtr ComponentLightSetColor;
            public IntPtr ComponentLightGetRange;
            public IntPtr ComponentLightSetRange;
            public IntPtr ComponentLightGetSpotAngle;
            public IntPtr ComponentLightSetSpotAngle;

            public IntPtr ComponentMaterialGetAlbedo;
            public IntPtr ComponentMaterialSetAlbedo;
            public IntPtr ComponentMaterialGetMetallic;
            public IntPtr ComponentMaterialSetMetallic;
            public IntPtr ComponentMaterialGetSmoothness;
            public IntPtr ComponentMaterialSetSmoothness;

            public IntPtr ComponentScriptGetEnabled;
            public IntPtr ComponentScriptSetEnabled;
            public IntPtr ComponentScriptGetTypeName;
            public IntPtr ComponentScriptGetFieldsJson;
            public IntPtr ComponentScriptSetFieldsJson;

            public IntPtr ComponentGetBool;
            public IntPtr ComponentSetBool;
            public IntPtr ComponentGetInt;
            public IntPtr ComponentSetInt;
            public IntPtr ComponentGetFloat;
            public IntPtr ComponentSetFloat;
            public IntPtr ComponentGetVector4;
            public IntPtr ComponentSetVector4;
            public IntPtr ComponentGetString;
            public IntPtr ComponentSetString;
            public IntPtr ComponentInvoke;
            public IntPtr NavigationFindPath;
            public IntPtr AnimationSetBool;
            public IntPtr ScriptDebugShouldBreak;
            public IntPtr ScriptDebugReportException;
            public IntPtr ScriptDebugReportBreakpoint;
        }

        /// <summary>
        /// Struct alocado em memória não-gerenciada que contém ponteiros para todas as funções
        /// gerenciadas (C#) que devem ser chamadas pelo runtime nativo.
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        private struct ManagedApiTable
        {
            public IntPtr CreateScriptInstance;
            public IntPtr DestroyScriptInstance;
            public IntPtr CallOnStart;
            public IntPtr CallOnUpdate;
            public IntPtr CallOnFixedUpdate;
            public IntPtr CallOnEnable;
            public IntPtr CallOnDisable;
            public IntPtr CallOnDestroy;
            public IntPtr GetPublicFieldsJson;
            public IntPtr SetFieldJson;
            public IntPtr ReloadScripts;
            public IntPtr CompileScriptsFolder;
            public IntPtr SafeInvokeOnCollisionEnter;
            public IntPtr SafeInvokeOnCollisionStay;
            public IntPtr SafeInvokeOnCollisionExit;
            public IntPtr SafeInvokeOnTriggerEnter;
            public IntPtr SafeInvokeOnTriggerStay;
            public IntPtr SafeInvokeOnTriggerExit;
            public IntPtr SafeInvokeOnUIClicked;
            public IntPtr SafeInvokeOnUIValueChanged;
            public IntPtr SafeInvokeOnUITextChanged;
            public IntPtr FreeManagedBuffer;
            public IntPtr GetGCStats;
        }

        private static IntPtr _managedApiTablePtr = IntPtr.Zero;
        // Os ponteiros expostos ao C++ não mantêm os delegates gerenciados vivos.
        // A tabela permanece ativa durante toda a aplicação, então os delegates
        // correspondentes também precisam permanecer enraizados nesse período.
        private static readonly List<Delegate> s_managedCallbackRoots = new();

        private static IntPtr GetManagedCallbackPointer(Delegate callback)
        {
            s_managedCallbackRoots.Add(callback);
            return Marshal.GetFunctionPointerForDelegate(callback);
        }

        /// <summary>
        /// Ponto de entrada chamado pelo lado C++ via nethost. Copia os ponteiros nativos para
        /// os delegates gerenciados e retorna o endereço da tabela ManagedAPI.
        /// </summary>
        [UnmanagedCallersOnly(EntryPoint = "InitializeBootstrap", CallConvs = new[] { typeof(CallConvCdecl) })]
        public static unsafe IntPtr InitializeBootstrap(IntPtr nativeApi)
        {
            if (_initialized)
            {
                return _managedApiTablePtr;
            }

            _initialized = true;
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

            try
            {
                NativeApiTable* nativeTable = (NativeApiTable*)nativeApi.ToPointer();
                WireNativeDelegates(nativeTable);
                ManagedApiTable managed = BuildManagedApiTable();
                _managedApiTablePtr = Marshal.AllocHGlobal(Marshal.SizeOf<ManagedApiTable>());
                Marshal.StructureToPtr(managed, _managedApiTablePtr, false);
                NativeDelegates.Log?.Invoke(0, "[Scripting] Managed bootstrap OK: managed API table exposed.");
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(3, "[Scripting] Managed bootstrap FAILED: " + ex);
            }

            return _managedApiTablePtr;
        }

        private static unsafe void WireNativeDelegates(NativeApiTable* t)
        {
            void Set<T>(IntPtr fn, ref T? del) where T : Delegate
            {
                if (fn != IntPtr.Zero)
                    del = Marshal.GetDelegateForFunctionPointer<T>(fn);
            }

            Set(t->Log, ref NativeDelegates.Log);

            Set(t->EntityCount, ref NativeDelegates.EntityCount);
            Set(t->EntityGetID, ref NativeDelegates.EntityGetID);
            Set(t->EntityGetName, ref NativeDelegates.EntityGetName);
            Set(t->EntitySetName, ref NativeDelegates.EntitySetName);
            Set(t->EntityApplyImpulse, ref NativeDelegates.EntityApplyImpulse);
            Set(t->EntityGetActive, ref NativeDelegates.EntityGetActive);
            Set(t->EntitySetActive, ref NativeDelegates.EntitySetActive);
            Set(t->EntityGetTag, ref NativeDelegates.EntityGetTag);
            Set(t->EntitySetTag, ref NativeDelegates.EntitySetTag);
            Set(t->EntityGetLayer, ref NativeDelegates.EntityGetLayer);
            Set(t->EntitySetLayer, ref NativeDelegates.EntitySetLayer);
            Set(t->EntityGetTransform, ref NativeDelegates.EntityGetTransform);

            Set(t->TransformGetLocalPosition, ref NativeDelegates.TransformGetLocalPosition);
            Set(t->TransformSetLocalPosition, ref NativeDelegates.TransformSetLocalPosition);
            Set(t->TransformGetWorldPosition, ref NativeDelegates.TransformGetWorldPosition);
            Set(t->TransformSetWorldPosition, ref NativeDelegates.TransformSetWorldPosition);
            Set(t->TransformGetLocalRotation, ref NativeDelegates.TransformGetLocalRotation);
            Set(t->TransformSetLocalRotation, ref NativeDelegates.TransformSetLocalRotation);
            Set(t->TransformGetWorldRotation, ref NativeDelegates.TransformGetWorldRotation);
            Set(t->TransformSetWorldRotation, ref NativeDelegates.TransformSetWorldRotation);
            Set(t->TransformGetLocalScale, ref NativeDelegates.TransformGetLocalScale);
            Set(t->TransformSetLocalScale, ref NativeDelegates.TransformSetLocalScale);
            Set(t->TransformGetEuler, ref NativeDelegates.TransformGetEuler);
            Set(t->TransformSetEuler, ref NativeDelegates.TransformSetEuler);
            Set(t->TransformTranslateDelta, ref NativeDelegates.TransformTranslateDelta);
            Set(t->TransformRotateEuler, ref NativeDelegates.TransformRotateEuler);
            Set(t->TransformLookAt, ref NativeDelegates.TransformLookAt);
            Set(t->TransformSetParent, ref NativeDelegates.TransformSetParent);
            Set(t->TransformGetChildCount, ref NativeDelegates.TransformGetChildCount);
            Set(t->TransformGetChild, ref NativeDelegates.TransformGetChild);
            Set(t->TransformGetParent, ref NativeDelegates.TransformGetParent);

            Set(t->EntityFindByName, ref NativeDelegates.EntityFindByName);
            Set(t->EntityFindByTag, ref NativeDelegates.EntityFindByTag);
            Set(t->EntityFindById, ref NativeDelegates.EntityFindById);
            Set(t->EntityFindAllByTag, ref NativeDelegates.EntityFindAllByTag);
            Set(t->EntityDestroy, ref NativeDelegates.EntityDestroy);
            Set(t->EntityDestroyHierarchy, ref NativeDelegates.EntityDestroyHierarchy);
            Set(t->EntityInstantiate, ref NativeDelegates.EntityInstantiate);
            Set(t->EntityHasUI, ref NativeDelegates.EntityHasUI);
            Set(t->UIGetType, ref NativeDelegates.UIGetType);
            Set(t->UIGetEnabled, ref NativeDelegates.UIGetEnabled);
            Set(t->UISetEnabled, ref NativeDelegates.UISetEnabled);
            Set(t->UIGetRect, ref NativeDelegates.UIGetRect);
            Set(t->UIIsPointerOver, ref NativeDelegates.UIIsPointerOver);

            Set(t->InputGetKey, ref NativeDelegates.InputGetKey);
            Set(t->InputGetKeyDown, ref NativeDelegates.InputGetKeyDown);
            Set(t->InputGetKeyUp, ref NativeDelegates.InputGetKeyUp);
            Set(t->InputGetMouseButton, ref NativeDelegates.InputGetMouseButton);
            Set(t->InputGetMouseButtonDown, ref NativeDelegates.InputGetMouseButtonDown);
            Set(t->InputGetMouseButtonUp, ref NativeDelegates.InputGetMouseButtonUp);
            Set(t->InputGetMousePos, ref NativeDelegates.InputGetMousePos);
            Set(t->InputGetAxis, ref NativeDelegates.InputGetAxis);
            Set(t->InputGetAction, ref NativeDelegates.InputGetAction);
            Set(t->InputGetActionDown, ref NativeDelegates.InputGetActionDown);
            Set(t->InputGetActionUp, ref NativeDelegates.InputGetActionUp);
            Set(t->InputBindKey, ref NativeDelegates.InputBindKey);
            Set(t->InputBindMouseButton, ref NativeDelegates.InputBindMouseButton);
            Set(t->InputClearBindings, ref NativeDelegates.InputClearBindings);
            Set(t->InputSetDeadZone, ref NativeDelegates.InputSetDeadZone);

            Set(t->TimeGetDelta, ref NativeDelegates.TimeGetDelta);
            Set(t->TimeGetUnscaledDelta, ref NativeDelegates.TimeGetUnscaledDelta);
            Set(t->TimeGetTime, ref NativeDelegates.TimeGetTime);
            Set(t->TimeGetFixedDelta, ref NativeDelegates.TimeGetFixedDelta);
            Set(t->TimeGetTimeScale, ref NativeDelegates.TimeGetTimeScale);
            Set(t->TimeSetTimeScale, ref NativeDelegates.TimeSetTimeScale);

            Set(t->EntityHasComponent, ref NativeDelegates.EntityHasComponent);
            Set(t->EntityGetComponent, ref NativeDelegates.EntityGetComponent);

            Set(t->ComponentMeshGetName, ref NativeDelegates.ComponentMeshGetName);
            Set(t->ComponentMeshSetName, ref NativeDelegates.ComponentMeshSetName);
            Set(t->ComponentMeshGetVisible, ref NativeDelegates.ComponentMeshGetVisible);
            Set(t->ComponentMeshSetVisible, ref NativeDelegates.ComponentMeshSetVisible);
            Set(t->ComponentMeshGetColor, ref NativeDelegates.ComponentMeshGetColor);
            Set(t->ComponentMeshSetColor, ref NativeDelegates.ComponentMeshSetColor);

            Set(t->ComponentAudioPlay, ref NativeDelegates.ComponentAudioPlay);
            Set(t->ComponentAudioStop, ref NativeDelegates.ComponentAudioStop);
            Set(t->ComponentAudioGetPlaying, ref NativeDelegates.ComponentAudioGetPlaying);
            Set(t->ComponentAudioGetVolume, ref NativeDelegates.ComponentAudioGetVolume);
            Set(t->ComponentAudioSetVolume, ref NativeDelegates.ComponentAudioSetVolume);
            Set(t->ComponentAudioGetLoop, ref NativeDelegates.ComponentAudioGetLoop);
            Set(t->ComponentAudioSetLoop, ref NativeDelegates.ComponentAudioSetLoop);
            Set(t->ComponentAudioGetPitch, ref NativeDelegates.ComponentAudioGetPitch);
            Set(t->ComponentAudioSetPitch, ref NativeDelegates.ComponentAudioSetPitch);
            Set(t->AudioPlayBySourceID, ref NativeDelegates.AudioPlayBySourceID);

            Set(t->ComponentLightGetType, ref NativeDelegates.ComponentLightGetType);
            Set(t->ComponentLightSetType, ref NativeDelegates.ComponentLightSetType);
            Set(t->ComponentLightGetIntensity, ref NativeDelegates.ComponentLightGetIntensity);
            Set(t->ComponentLightSetIntensity, ref NativeDelegates.ComponentLightSetIntensity);
            Set(t->ComponentLightGetColor, ref NativeDelegates.ComponentLightGetColor);
            Set(t->ComponentLightSetColor, ref NativeDelegates.ComponentLightSetColor);
            Set(t->ComponentLightGetRange, ref NativeDelegates.ComponentLightGetRange);
            Set(t->ComponentLightSetRange, ref NativeDelegates.ComponentLightSetRange);
            Set(t->ComponentLightGetSpotAngle, ref NativeDelegates.ComponentLightGetSpotAngle);
            Set(t->ComponentLightSetSpotAngle, ref NativeDelegates.ComponentLightSetSpotAngle);

            Set(t->ComponentMaterialGetAlbedo, ref NativeDelegates.ComponentMaterialGetAlbedo);
            Set(t->ComponentMaterialSetAlbedo, ref NativeDelegates.ComponentMaterialSetAlbedo);
            Set(t->ComponentMaterialGetMetallic, ref NativeDelegates.ComponentMaterialGetMetallic);
            Set(t->ComponentMaterialSetMetallic, ref NativeDelegates.ComponentMaterialSetMetallic);
            Set(t->ComponentMaterialGetSmoothness, ref NativeDelegates.ComponentMaterialGetSmoothness);
            Set(t->ComponentMaterialSetSmoothness, ref NativeDelegates.ComponentMaterialSetSmoothness);

            Set(t->ComponentScriptGetEnabled, ref NativeDelegates.ComponentScriptGetEnabled);
            Set(t->ComponentScriptSetEnabled, ref NativeDelegates.ComponentScriptSetEnabled);
            Set(t->ComponentScriptGetTypeName, ref NativeDelegates.ComponentScriptGetTypeName);
            Set(t->ComponentScriptGetFieldsJson, ref NativeDelegates.ComponentScriptGetFieldsJson);
            Set(t->ComponentScriptSetFieldsJson, ref NativeDelegates.ComponentScriptSetFieldsJson);

            Set(t->ComponentGetBool, ref NativeDelegates.ComponentGetBool);
            Set(t->ComponentSetBool, ref NativeDelegates.ComponentSetBool);
            Set(t->ComponentGetInt, ref NativeDelegates.ComponentGetInt);
            Set(t->ComponentSetInt, ref NativeDelegates.ComponentSetInt);
            Set(t->ComponentGetFloat, ref NativeDelegates.ComponentGetFloat);
            Set(t->ComponentSetFloat, ref NativeDelegates.ComponentSetFloat);
            Set(t->ComponentGetVector4, ref NativeDelegates.ComponentGetVector4);
            Set(t->ComponentSetVector4, ref NativeDelegates.ComponentSetVector4);
            Set(t->ComponentGetString, ref NativeDelegates.ComponentGetString);
            Set(t->ComponentSetString, ref NativeDelegates.ComponentSetString);
            Set(t->ComponentInvoke, ref NativeDelegates.ComponentInvoke);
            Set(t->NavigationFindPath, ref NativeDelegates.NavigationFindPath);
            Set(t->AnimationSetBool, ref NativeDelegates.AnimationSetBool);
            Set(t->ScriptDebugShouldBreak, ref NativeDelegates.ScriptDebugShouldBreak);
            Set(t->ScriptDebugReportException, ref NativeDelegates.ScriptDebugReportException);
            Set(t->ScriptDebugReportBreakpoint, ref NativeDelegates.ScriptDebugReportBreakpoint);
        }

        private static ManagedApiTable BuildManagedApiTable()
        {
            CreateScriptInstanceDlg createDlg = ScriptInvoker.CreateScriptInstance;
            DestroyScriptInstanceDlg destroyDlg = ScriptInvoker.DestroyScriptInstance;
            CallUpdateDlg onUpdateDlg = ScriptInvoker.CallOnUpdate;
            CallUpdateDlg onFixedUpdateDlg = ScriptInvoker.CallOnFixedUpdate;
            GetFieldsDlg getFieldsDlg = ScriptInvoker.GetPublicFieldsJson;
            SetFieldDlg setFieldDlg = ScriptInvoker.SetFieldJson;
            ReloadScriptsDlg reloadDlg = AssemblyManager.ReloadScripts;
            CompileScriptsFolderDlg compileDlg = AssemblyManager.CompileScriptsFolder;

            CallOneDlg onStartDlg = delegate(IntPtr p) { return ScriptInvoker.CallOnStart(p); };
            CallOneDlg onEnableDlg = delegate(IntPtr p) { return ScriptInvoker.CallOnEnable(p); };
            CallOneDlg onDisableDlg = delegate(IntPtr p) { return ScriptInvoker.CallOnDisable(p); };
            CallOneDlg onDestroyDlg = delegate(IntPtr p) { return ScriptInvoker.CallOnDestroy(p); };

            CallCollisionDlg collisionEnter = delegate(IntPtr inst, ulong o, float cx, float cy, float cz, float nx, float ny, float nz, float imp)
            {
                return ScriptInvoker.CallOnCollisionEnter(inst, o, new Vector3(cx, cy, cz), new Vector3(nx, ny, nz), imp);
            };
            CallCollisionDlg collisionStay = delegate(IntPtr inst, ulong o, float cx, float cy, float cz, float nx, float ny, float nz, float imp)
            {
                return ScriptInvoker.CallOnCollisionStay(inst, o, new Vector3(cx, cy, cz), new Vector3(nx, ny, nz), imp);
            };
            CallCollisionDlg collisionExit = delegate(IntPtr inst, ulong o, float cx, float cy, float cz, float nx, float ny, float nz, float imp)
            {
                return ScriptInvoker.CallOnCollisionExit(inst, o, new Vector3(cx, cy, cz), new Vector3(nx, ny, nz), imp);
            };

            CallTriggerDlg triggerEnter = delegate(IntPtr inst, ulong o) { return ScriptInvoker.CallOnTriggerEnter(inst, o); };
            CallTriggerDlg triggerStay = delegate(IntPtr inst, ulong o) { return ScriptInvoker.CallOnTriggerStay(inst, o); };
            CallTriggerDlg triggerExit = delegate(IntPtr inst, ulong o) { return ScriptInvoker.CallOnTriggerExit(inst, o); };

            CallUIDlg uiClicked = delegate(IntPtr inst, ulong entityId) { return ScriptInvoker.CallOnUIClicked(inst, entityId); };
            CallUIValueDlg uiValueChanged = delegate(IntPtr inst, ulong entityId, float value) { return ScriptInvoker.CallOnUIValueChanged(inst, entityId, value); };
            CallUITextDlg uiTextChanged = delegate(IntPtr inst, ulong entityId, string text) { return ScriptInvoker.CallOnUITextChanged(inst, entityId, text); };

            GetGCStatsDlg gcStatsDlg = delegate(out int g0, out int g1, out int g2, out long heap)
            {
                g0 = GC.CollectionCount(0);
                g1 = GC.CollectionCount(1);
                g2 = GC.CollectionCount(2);
                heap = GC.GetTotalMemory(false);
                return 0;
            };

            return new ManagedApiTable
            {
                CreateScriptInstance = GetManagedCallbackPointer(createDlg),
                DestroyScriptInstance = GetManagedCallbackPointer(destroyDlg),
                CallOnStart = GetManagedCallbackPointer(onStartDlg),
                CallOnUpdate = GetManagedCallbackPointer(onUpdateDlg),
                CallOnFixedUpdate = GetManagedCallbackPointer(onFixedUpdateDlg),
                CallOnEnable = GetManagedCallbackPointer(onEnableDlg),
                CallOnDisable = GetManagedCallbackPointer(onDisableDlg),
                CallOnDestroy = GetManagedCallbackPointer(onDestroyDlg),
                GetPublicFieldsJson = GetManagedCallbackPointer(getFieldsDlg),
                SetFieldJson = GetManagedCallbackPointer(setFieldDlg),
                ReloadScripts = GetManagedCallbackPointer(reloadDlg),
                CompileScriptsFolder = GetManagedCallbackPointer(compileDlg),
                SafeInvokeOnCollisionEnter = GetManagedCallbackPointer(collisionEnter),
                SafeInvokeOnCollisionStay = GetManagedCallbackPointer(collisionStay),
                SafeInvokeOnCollisionExit = GetManagedCallbackPointer(collisionExit),
                SafeInvokeOnTriggerEnter = GetManagedCallbackPointer(triggerEnter),
                SafeInvokeOnTriggerStay = GetManagedCallbackPointer(triggerStay),
                SafeInvokeOnTriggerExit = GetManagedCallbackPointer(triggerExit),
                SafeInvokeOnUIClicked = GetManagedCallbackPointer(uiClicked),
                SafeInvokeOnUIValueChanged = GetManagedCallbackPointer(uiValueChanged),
                SafeInvokeOnUITextChanged = GetManagedCallbackPointer(uiTextChanged),
                FreeManagedBuffer = GetManagedCallbackPointer(FreeManagedBufferCallback),
                GetGCStats = GetManagedCallbackPointer(gcStatsDlg),
            };
        }
    }
}
