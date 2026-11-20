#pragma once

// Câmera de cena, independente da câmera usada para navegar no editor.
// O preview no Inspector usa o Transform da entidade e estes parâmetros.
struct CameraComponent
{
    bool Enabled = true;
    bool PreviewEnabled = true;
    bool ShowFrustumGizmo = true;
    bool Orthographic = false;
    float FieldOfView = 60.0f;
    float NearClip = 0.1f;
    float FarClip = 10000.0f;
    float OrthographicSize = 10.0f;
};
