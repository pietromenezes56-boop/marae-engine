#pragma once

#include "../../Scene/Scene/Scene.h"

class AISystem
{
public:
    // Call only while the game is in Play. dt is clamped internally to avoid
    // giant movement/timer steps after debugger pauses or window stalls.
    static void Update(Scene& scene, float deltaTime);
    static int RunRegressionTests();
};

