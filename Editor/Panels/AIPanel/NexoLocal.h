#pragma once

#include <string>

// Planejador determinístico, local e econômico do Nexo.
// Não responde conhecimento geral nem executa código arbitrário.
// Retorna actions como JSON no mesmo contrato usado pelo AIPanel.
bool NexoLocalPlan(const std::string& request,
                   std::string& reply,
                   std::string& actionsJson,
                   std::string& searchQuery);
