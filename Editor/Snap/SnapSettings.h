#pragma once

struct SnapSettings
{
    bool PositionEnabled = false;
    bool RotationEnabled = false;
    bool ScaleEnabled = false;

    float PositionStep = 1.0f;
    float RotationStep = 15.0f;
    float ScaleStep = 0.1f;
    // Geometric docking is independent from the three numeric transform snaps above.
    bool FaceSnapEnabled = true;
    bool FaceAlignmentEnabled = true;
    bool EdgeSnapEnabled = true;
    bool AxisAlignmentEnabled = true;
    float FaceSnapThreshold = 0.25f; // distance to trigger snap (world units)
    float FaceAlignTolerance = 0.25f; // tolerance for alignment on the other two axes
    bool VertexSnapEnabled = true;
    float VertexSnapThreshold = 0.35f;
    float EdgeSnapThreshold = 0.25f;
};
