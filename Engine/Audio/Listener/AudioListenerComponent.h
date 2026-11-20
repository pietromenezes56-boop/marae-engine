#pragma once
// ---------------------------------------------------------------------------
// AudioListenerComponent — componente que define o ponto de escuta 3D.
//
// Deve existir exatamente UM listener ativo na cena. O AudioSystem usa o
// primeiro listener ativo encontrado.
//
// A posição/orientação vêm do Transform da entidade a cada frame.
// A velocidade é derivada da variação de posição entre frames (para Doppler).
// ---------------------------------------------------------------------------
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct AudioListenerComponent
{
    bool  Enabled         = true;

    // Escala global de volume para este listener (normalmente 1)
    float VolumeScale     = 1.0f;

    // Se verdadeiro, o AudioSystem atualiza automaticamente a posição/orientação
    // a partir do Transform da entidade. Se falso, usa os campos abaixo.
    bool  AutoFollowTransform = true;

    // ---- Posição/orientação manual (usados quando AutoFollow=false) ------
    glm::vec3 ManualPosition  = glm::vec3(0.f);
    glm::vec3 ManualForward   = glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 ManualUp        = glm::vec3(0.f, 1.f,  0.f);

    // ---- Estado calculado por frame (não serializado) -------------------
    glm::vec3 WorldPosition   = glm::vec3(0.f);
    glm::vec3 WorldForward    = glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 WorldUp         = glm::vec3(0.f, 1.f,  0.f);
    glm::vec3 Velocity        = glm::vec3(0.f);   // m/s, derivada de posição

    // Índice do listener no miniaudio (sempre 0 para projetos com 1 listener)
    uint32_t ListenerIndex    = 0;
};
