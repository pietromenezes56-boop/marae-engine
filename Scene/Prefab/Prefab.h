#pragma once

#include <string>

class Entity;
class Scene;

// ---------------------------------------------------------------------------
// Prefab — sistema de prefabs vinculados
//
// Um .prefab armazena o estado completo de uma entidade (transform, mesh,
// material, cor, luz, física, áudio, metadados).  Toda entidade instanciada
// via Prefab::Instantiate recebe seu PrefabPath preenchido, criando o vínculo.
//
// Operações disponíveis:
//   Save               — salva o estado atual de uma entidade no arquivo .prefab
//   Load               — carrega um .prefab numa entidade já existente
//   Instantiate        — cria nova entidade na cena a partir de um .prefab
//   ApplyToFile        — aplica as mudanças de uma instância de volta ao .prefab
//   RevertInstance     — restaura a instância ao estado do .prefab
//   ApplyToAllInstances— propaga o .prefab para todas as instâncias na cena
// ---------------------------------------------------------------------------
class Prefab
{
public:
    // Salva o estado da entidade no arquivo .prefab.
    // Se scene != nullptr e a entidade tiver filhos, salva a hierarquia completa recursivamente.
    static bool Save(const Entity& entity, const std::string& path, const Scene* scene = nullptr);

    // Carrega um .prefab numa entidade existente (não altera PrefabPath nem cria filhos).
    static bool Load(Entity& entity, const std::string& path);

    // Cria nova entidade (e todos os seus filhos recursivos se for hierárquico) a partir do .prefab.
    // Vincula a raiz (e opcionalmente filhos) preenchendo PrefabPath.
    static Entity& Instantiate(Scene& scene, const std::string& path);

    // Sobrescreve o arquivo .prefab com o estado atual da entidade-instância e sua hierarquia de filhos.
    // Retorna false se a entidade não tiver PrefabPath definido.
    static bool ApplyToFile(const Entity& entity, const Scene* scene = nullptr);

    // Restaura a entidade (e seus filhos de prefab se scene != nullptr) ao estado salvo no .prefab.
    static bool RevertInstance(Entity& entity, Scene* scene = nullptr);

    // Aplica o .prefab a todas as instâncias na cena que compartilham o mesmo PrefabPath.
    // Preserva a posição/rotação/escala mundial de cada instância raiz se keepTransform == true.
    // Recria / atualiza todos os nós filhos vinculados ao prefab em cada instância!
    static void ApplyToAllInstances(Scene& scene,
                                    const std::string& prefabPath,
                                    bool keepTransform = true);

    // Verifica se o arquivo .prefab contém uma hierarquia com múltiplos nós/filhos
    static bool HasHierarchy(const std::string& path);

    // Desvincula uma entidade e todos os seus filhos recursivos de qualquer prefab
    static void UnlinkHierarchy(Scene& scene, Entity& entity);

    // Testes determinísticos do ciclo salvar/instanciar/atualizar/reverter.
    // Retorna 0 quando todas as invariantes de overrides passam.
    static int RunRegressionTests();
};
