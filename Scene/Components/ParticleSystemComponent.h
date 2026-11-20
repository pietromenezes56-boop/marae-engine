#pragma once

#include <algorithm>
#include <string>
#include <glm/glm.hpp>

// Emissor leve de sprites billboard 2D dentro da cena 3D.
struct ParticleSystemComponent
{
    bool Enabled = true;
    int MaxParticles = 64;
    float EmissionRate = 16.0f;
    float Lifetime = 1.5f;
    float Speed = 1.5f;
    float Spread = 0.45f;
    float StartSize = 0.18f;
    float EndSize = 0.04f;
    int Shape = 0; // 0 circle, 1 square, 2 triangle, 3 diamond, 4 pentagon, 5 star
    float Glow = 0.0f;
    int EmissionShape = 0; // 0 point, 1 sphere, 2 box
    int BurstCount = 1;
    float SpeedVariation = 0.35f;
    float LifetimeVariation = 0.0f;
    float Drag = 0.0f;
    glm::vec3 Wind{ 0.0f };
    float Turbulence = 0.0f;
    float StartRotation = 0.0f;
    float RotationSpeed = 0.0f;
    float MidSize = 0.0f; // zero = linear interpolation
    glm::vec4 MidColor{ 1.0f };
    bool UseMidColor = false;
    bool Lit = false;
    std::string TexturePath;
    glm::vec3 Gravity{ 0.0f, -0.35f, 0.0f };
    glm::vec4 StartColor{ 1.0f, 0.55f, 0.12f, 1.0f };
    glm::vec4 EndColor{ 0.35f, 0.12f, 0.02f, 0.0f };

    void Clamp()
    {
        MaxParticles = std::clamp(MaxParticles, 1, 512);
        EmissionRate = std::clamp(EmissionRate, 0.1f, 256.0f);
        Lifetime = std::clamp(Lifetime, 0.05f, 30.0f);
        Speed = std::clamp(Speed, 0.0f, 100.0f);
        Spread = std::clamp(Spread, 0.0f, 3.14159f);
        StartSize = std::clamp(StartSize, 0.001f, 20.0f);
        EndSize = std::clamp(EndSize, 0.001f, 20.0f);
        Shape = std::clamp(Shape, 0, 5);
        Glow = std::clamp(Glow, 0.0f, 5.0f);
        EmissionShape = std::clamp(EmissionShape, 0, 2);
        BurstCount = std::clamp(BurstCount, 1, 64);
        SpeedVariation = std::clamp(SpeedVariation, 0.0f, 1.0f);
        LifetimeVariation = std::clamp(LifetimeVariation, 0.0f, 0.95f);
        Drag = std::clamp(Drag, 0.0f, 20.0f);
        Wind = glm::clamp(Wind, glm::vec3(-100.0f), glm::vec3(100.0f));
        Turbulence = std::clamp(Turbulence, 0.0f, 50.0f);
        StartRotation = std::clamp(StartRotation, -6.28319f, 6.28319f);
        RotationSpeed = std::clamp(RotationSpeed, -100.0f, 100.0f);
        MidSize = std::clamp(MidSize, 0.0f, 20.0f);
        MidColor = glm::clamp(MidColor, glm::vec4(0.0f), glm::vec4(1.0f));
        Gravity = glm::clamp(Gravity, glm::vec3(-100.0f), glm::vec3(100.0f));
        StartColor = glm::clamp(StartColor, glm::vec4(0.0f), glm::vec4(1.0f));
        EndColor = glm::clamp(EndColor, glm::vec4(0.0f), glm::vec4(1.0f));
    }
};
