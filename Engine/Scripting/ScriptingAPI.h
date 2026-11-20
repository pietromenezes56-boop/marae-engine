#pragma once

#include <cstdint>

enum class ComponentType : int
{
    Mesh = 1,
    Audio = 2,
    Light = 3,
    Material = 4,
    Script = 5,
    Physics = 6,
    UI = 7,
    Animation = 8,
    Camera = 9,
    GlobalLight = 10
};

struct NativeAPI
{
    // Logging
    void (*Log)(int level, const wchar_t* msg) = nullptr;

    // Entity Queries
    int (*EntityCount)() = nullptr;
    uint64_t (*EntityGetID)(uint64_t entityIndex) = nullptr;
    const wchar_t* (*EntityGetName)(uint64_t entityId) = nullptr;
    void (*EntitySetName)(uint64_t entityId, const wchar_t* name) = nullptr;
    void (*EntityApplyImpulse)(uint64_t entityId, float x, float y, float z) = nullptr;
    bool (*EntityGetActive)(uint64_t entityId) = nullptr;
    void (*EntitySetActive)(uint64_t entityId, bool active) = nullptr;
    const wchar_t* (*EntityGetTag)(uint64_t entityId) = nullptr;
    void (*EntitySetTag)(uint64_t entityId, const wchar_t* tag) = nullptr;
    int (*EntityGetLayer)(uint64_t entityId) = nullptr;
    void (*EntitySetLayer)(uint64_t entityId, int layer) = nullptr;
    uint64_t (*EntityGetTransform)(uint64_t entityId) = nullptr;

    // Transform
    void (*TransformGetLocalPosition)(uint64_t transformHandle, float* outX, float* outY, float* outZ) = nullptr;
    void (*TransformSetLocalPosition)(uint64_t transformHandle, float x, float y, float z) = nullptr;
    void (*TransformGetWorldPosition)(uint64_t h, float* ox, float* oy, float* oz) = nullptr;
    void (*TransformSetWorldPosition)(uint64_t h, float x, float y, float z) = nullptr;
    void (*TransformGetLocalRotation)(uint64_t h, float* ox, float* oy, float* oz, float* ow) = nullptr;
    void (*TransformSetLocalRotation)(uint64_t h, float x, float y, float z, float w) = nullptr;
    void (*TransformGetWorldRotation)(uint64_t h, float* ox, float* oy, float* oz, float* ow) = nullptr;
    void (*TransformSetWorldRotation)(uint64_t h, float x, float y, float z, float w) = nullptr;
    void (*TransformGetLocalScale)(uint64_t h, float* ox, float* oy, float* oz) = nullptr;
    void (*TransformSetLocalScale)(uint64_t h, float x, float y, float z) = nullptr;
    void (*TransformGetEuler)(uint64_t h, float* ox, float* oy, float* oz) = nullptr;
    void (*TransformSetEuler)(uint64_t h, float x, float y, float z) = nullptr;
    void (*TransformTranslateDelta)(uint64_t h, float dx, float dy, float dz, int space) = nullptr;
    void (*TransformRotateEuler)(uint64_t h, float rx, float ry, float rz, int space) = nullptr;
    void (*TransformLookAt)(uint64_t h, float tx, float ty, float tz) = nullptr;
    void (*TransformSetParent)(uint64_t transformHandle, uint64_t parentEntityId, bool worldPositionStays) = nullptr;
    int (*TransformGetChildCount)(uint64_t transformHandle) = nullptr;
    uint64_t (*TransformGetChild)(uint64_t transformHandle, int index) = nullptr;
    uint64_t (*TransformGetParent)(uint64_t transformHandle) = nullptr;

    // Entity Find / Destroy / Instantiate
    uint64_t (*EntityFindByName)(const wchar_t* name) = nullptr;
    uint64_t (*EntityFindByTag)(const wchar_t* tag) = nullptr;
    uint64_t (*EntityFindById)(uint64_t id) = nullptr;
    void* (*EntityFindAllByTag)(const wchar_t* tag, int* outCount) = nullptr;
    void (*EntityDestroy)(uint64_t entityId) = nullptr;
    void (*EntityDestroyHierarchy)(uint64_t entityId) = nullptr;
    uint64_t (*EntityInstantiate)(uint64_t sourceEntityId) = nullptr;

    // UI 2D
    bool (*EntityHasUI)(uint64_t entityId) = nullptr;
    int (*UIGetType)(uint64_t entityId) = nullptr;
    bool (*UIGetEnabled)(uint64_t entityId) = nullptr;
    void (*UISetEnabled)(uint64_t entityId, bool enabled) = nullptr;
    void (*UIGetRect)(uint64_t entityId, float* x, float* y, float* width, float* height) = nullptr;
    bool (*UIIsPointerOver)(uint64_t entityId) = nullptr;

    // Input
    bool (*InputGetKey)(int keycode) = nullptr;
    bool (*InputGetKeyDown)(int keycode) = nullptr;
    bool (*InputGetKeyUp)(int keycode) = nullptr;
    bool (*InputGetMouseButton)(int button) = nullptr;
    bool (*InputGetMouseButtonDown)(int button) = nullptr;
    bool (*InputGetMouseButtonUp)(int button) = nullptr;
    void (*InputGetMousePos)(float* ox, float* oy, float* oz) = nullptr;
    float (*InputGetAxis)(const wchar_t* axisName) = nullptr;
    bool (*InputGetAction)(const wchar_t* actionName) = nullptr;
    bool (*InputGetActionDown)(const wchar_t* actionName) = nullptr;
    bool (*InputGetActionUp)(const wchar_t* actionName) = nullptr;
    void (*InputBindKey)(const wchar_t* actionName, int keyCode, float scale) = nullptr;
    void (*InputBindMouseButton)(const wchar_t* actionName, int button, float scale) = nullptr;
    void (*InputClearBindings)(const wchar_t* actionName) = nullptr;
    void (*InputSetDeadZone)(const wchar_t* axisName, float deadZone) = nullptr;

    // Time
    float (*TimeGetDelta)() = nullptr;
    float (*TimeGetUnscaledDelta)() = nullptr;
    float (*TimeGetTime)() = nullptr;
    float (*TimeGetFixedDelta)() = nullptr;
    float (*TimeGetTimeScale)() = nullptr;
    void (*TimeSetTimeScale)(float value) = nullptr;

    // Component queries
    bool (*EntityHasComponent)(uint64_t entityId, int componentType) = nullptr;
    uint64_t (*EntityGetComponent)(uint64_t entityId, int componentType) = nullptr;

    // Mesh Component
    const wchar_t* (*ComponentMeshGetName)(uint64_t entityId) = nullptr;
    void (*ComponentMeshSetName)(uint64_t entityId, const wchar_t* name) = nullptr;
    bool (*ComponentMeshGetVisible)(uint64_t entityId) = nullptr;
    void (*ComponentMeshSetVisible)(uint64_t entityId, bool visible) = nullptr;
    void (*ComponentMeshGetColor)(uint64_t entityId, float* r, float* g, float* b, float* a) = nullptr;
    void (*ComponentMeshSetColor)(uint64_t entityId, float r, float g, float b, float a) = nullptr;

    // Audio Component
    void (*ComponentAudioPlay)(uint64_t entityId) = nullptr;
    void (*ComponentAudioStop)(uint64_t entityId) = nullptr;
    bool (*ComponentAudioGetPlaying)(uint64_t entityId) = nullptr;
    float (*ComponentAudioGetVolume)(uint64_t entityId) = nullptr;
    void (*ComponentAudioSetVolume)(uint64_t entityId, float volume) = nullptr;
    bool (*ComponentAudioGetLoop)(uint64_t entityId) = nullptr;
    void (*ComponentAudioSetLoop)(uint64_t entityId, bool loop) = nullptr;
    float (*ComponentAudioGetPitch)(uint64_t entityId) = nullptr;
    void (*ComponentAudioSetPitch)(uint64_t entityId, float pitch) = nullptr;
    bool (*AudioPlayBySourceID)(const wchar_t* sourceID) = nullptr;

    // Light Component
    int (*ComponentLightGetType)(uint64_t entityId) = nullptr;
    void (*ComponentLightSetType)(uint64_t entityId, int type) = nullptr;
    float (*ComponentLightGetIntensity)(uint64_t entityId) = nullptr;
    void (*ComponentLightSetIntensity)(uint64_t entityId, float intensity) = nullptr;
    void (*ComponentLightGetColor)(uint64_t entityId, float* r, float* g, float* b, float* a) = nullptr;
    void (*ComponentLightSetColor)(uint64_t entityId, float r, float g, float b, float a) = nullptr;
    float (*ComponentLightGetRange)(uint64_t entityId) = nullptr;
    void (*ComponentLightSetRange)(uint64_t entityId, float range) = nullptr;
    float (*ComponentLightGetSpotAngle)(uint64_t entityId) = nullptr;
    void (*ComponentLightSetSpotAngle)(uint64_t entityId, float angle) = nullptr;

    // Material Component
    void (*ComponentMaterialGetAlbedo)(uint64_t entityId, float* r, float* g, float* b, float* a) = nullptr;
    void (*ComponentMaterialSetAlbedo)(uint64_t entityId, float r, float g, float b, float a) = nullptr;
    float (*ComponentMaterialGetMetallic)(uint64_t entityId) = nullptr;
    void (*ComponentMaterialSetMetallic)(uint64_t entityId, float metallic) = nullptr;
    float (*ComponentMaterialGetSmoothness)(uint64_t entityId) = nullptr;
    void (*ComponentMaterialSetSmoothness)(uint64_t entityId, float smoothness) = nullptr;

    // Script Component
    bool (*ComponentScriptGetEnabled)(uint64_t entityId) = nullptr;
    void (*ComponentScriptSetEnabled)(uint64_t entityId, bool enabled) = nullptr;
    const wchar_t* (*ComponentScriptGetTypeName)(uint64_t entityId) = nullptr;
    const wchar_t* (*ComponentScriptGetFieldsJson)(uint64_t entityId) = nullptr;
    void (*ComponentScriptSetFieldsJson)(uint64_t entityId, const wchar_t* json) = nullptr;

    // Extensible component property bridge. Property IDs are declared in the
    // managed SDK; keeping this generic avoids ABI churn for each new property.
    bool (*ComponentGetBool)(uint64_t entityId, int componentType, int property) = nullptr;
    void (*ComponentSetBool)(uint64_t entityId, int componentType, int property, bool value) = nullptr;
    int (*ComponentGetInt)(uint64_t entityId, int componentType, int property) = nullptr;
    void (*ComponentSetInt)(uint64_t entityId, int componentType, int property, int value) = nullptr;
    float (*ComponentGetFloat)(uint64_t entityId, int componentType, int property) = nullptr;
    void (*ComponentSetFloat)(uint64_t entityId, int componentType, int property, float value) = nullptr;
    void (*ComponentGetVector4)(uint64_t entityId, int componentType, int property,
                                float* x, float* y, float* z, float* w) = nullptr;
    void (*ComponentSetVector4)(uint64_t entityId, int componentType, int property,
                                float x, float y, float z, float w) = nullptr;
    const wchar_t* (*ComponentGetString)(uint64_t entityId, int componentType, int property) = nullptr;
    void (*ComponentSetString)(uint64_t entityId, int componentType, int property,
                               const wchar_t* value) = nullptr;
    void (*ComponentInvoke)(uint64_t entityId, int componentType, int action) = nullptr;

    // NavMesh: retorna pontos no formato "x,y,z;x,y,z" em memória thread-local.
    const wchar_t* (*NavigationFindPath)(float sx, float sy, float sz,
                                         float dx, float dy, float dz) = nullptr;
    void (*AnimationSetBool)(uint64_t entityId, const wchar_t* parameter, bool value) = nullptr;
    // Mantidos no fim para preservar o ABI dos scripts existentes.
    bool (*ScriptDebugShouldBreak)(uint64_t entityId, const wchar_t* typeName,
                                   const wchar_t* methodName) = nullptr;
    void (*ScriptDebugReportException)(uint64_t entityId, const wchar_t* typeName,
                                       const wchar_t* methodName, const wchar_t* message,
                                       const wchar_t* stackTrace,
                                       const wchar_t* variablesJson) = nullptr;
    void (*ScriptDebugReportBreakpoint)(uint64_t entityId, const wchar_t* typeName,
                                       const wchar_t* methodName,
                                       const wchar_t* variablesJson) = nullptr;
};

struct ManagedAPI
{
    uint64_t (*CreateScriptInstance)(uint64_t entityId, const wchar_t* assemblyQualifiedTypeName) = nullptr;
    void (*DestroyScriptInstance)(uint64_t instanceHandle) = nullptr;
    int (*CallOnStart)(uint64_t instanceHandle) = nullptr;
    int (*CallOnUpdate)(uint64_t instanceHandle, float deltaTime) = nullptr;
    int (*CallOnFixedUpdate)(uint64_t instanceHandle, float fixedDeltaTime) = nullptr;
    int (*CallOnEnable)(uint64_t instanceHandle) = nullptr;
    int (*CallOnDisable)(uint64_t instanceHandle) = nullptr;
    int (*CallOnDestroy)(uint64_t instanceHandle) = nullptr;
    const wchar_t* (*GetPublicFieldsJson)(uint64_t instanceHandle) = nullptr;
    int (*SetFieldJson)(uint64_t instanceHandle, const wchar_t* fieldName, const wchar_t* jsonValue) = nullptr;
    int (*ReloadScripts)(const wchar_t* scriptsFolder, const wchar_t* outputFolder) = nullptr;
    int (*CompileScriptsFolder)(const wchar_t* scriptsFolder, const wchar_t* outputFolder, void** errors, int* errorCount) = nullptr;
    int (*SafeInvokeOnCollisionEnter)(uint64_t instanceHandle, uint64_t otherEntity, float cx, float cy, float cz, float nx, float ny, float nz, float impulse) = nullptr;
    int (*SafeInvokeOnCollisionStay)(uint64_t instanceHandle, uint64_t otherEntity, float cx, float cy, float cz, float nx, float ny, float nz, float impulse) = nullptr;
    int (*SafeInvokeOnCollisionExit)(uint64_t instanceHandle, uint64_t otherEntity, float cx, float cy, float cz, float nx, float ny, float nz, float impulse) = nullptr;
    int (*SafeInvokeOnTriggerEnter)(uint64_t instanceHandle, uint64_t otherEntity) = nullptr;
    int (*SafeInvokeOnTriggerStay)(uint64_t instanceHandle, uint64_t otherEntity) = nullptr;
    int (*SafeInvokeOnTriggerExit)(uint64_t instanceHandle, uint64_t otherEntity) = nullptr;
    // Eventos de UI 2D (a entidade é o elemento de UI que disparou o evento).
    int (*SafeInvokeOnUIClicked)(uint64_t instanceHandle, uint64_t entityId) = nullptr;
    int (*SafeInvokeOnUIValueChanged)(uint64_t instanceHandle, uint64_t entityId, float value) = nullptr;
    int (*SafeInvokeOnUITextChanged)(uint64_t instanceHandle, uint64_t entityId, const wchar_t* text) = nullptr;
    void (*FreeManagedBuffer)(void* buffer) = nullptr;
    // Diagnóstico do profiler: contadores do GC gerenciado. Mantido no fim
    // para preservar o ABI das entradas existentes.
    int (*GetGCStats)(int* gen0Collections, int* gen1Collections,
                      int* gen2Collections, long long* managedHeapBytes) = nullptr;
};

using InitializeBootstrapFn = ManagedAPI* (*)(NativeAPI* nativeApi);
