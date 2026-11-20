#pragma once

#include "Texture.h"

#include <string>

class TextureLoader
{
public:
	static Texture Load(
		const std::string& path
	);
};
