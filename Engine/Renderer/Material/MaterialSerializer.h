#pragma once

#include "Material.h"
#include <string>

class MaterialSerializer
{
public:

    static Material Load(
        const std::string& path
    );

    static void Save(
        const Material& material,
        const std::string& path
    );
};